// =============================================================================
// render/RaylibRenderer.cpp
//
// Purpose:
//   Implements the RaylibRenderer declared in RaylibRenderer.h. This is the
//   ONLY translation unit in the project that includes "raylib.h" - the rest of
//   the codebase remains 100 % free of raylib types, so it compiles unchanged
//   whether or not raylib is on the build path (R8.2, R28.4).
//
//   Every magic number that would otherwise be sprinkled through the drawing
//   code (window size, cell size, panel widths, font sizes, palette colours)
//   lives in the anonymous-namespace block at the top of this file (R8.5). The
//   helpers downstream pull from those constants by name, which makes balancing
//   the layout (e.g. trying a 32 px cell or a darker palette) a single-edit
//   change with no hunting through the file.
// =============================================================================

#include "render/RaylibRenderer.h"

// Raylib headers - only this translation unit may include them, so the rest of
// the project does not depend on raylib at compile or link time.
#include "raylib.h" // raylib C API: InitWindow, DrawRectangle, IsKeyPressed, ...

// Standard-library headers used only here (keeps the .h lean).
#include <algorithm> // std::clamp, std::max, std::min - HP/charge bar fills.
#include <cmath>     // std::sin - pulsing the full charge bar; GetTime() drives it.
#include <cstdint>   // std::int16_t - PCM sample type for procedural Sound buffers.
#include <cstdio>    // std::snprintf - format short HUD strings without iostreams.
#include <cstdlib>   // std::rand, RAND_MAX - white-noise samples for sound effects.
#include <cstring>   // std::memset - zero-fill audio sample buffers.
#include <sstream>   // std::istringstream - splits drawMessage on '\n'.
#include <string>    // std::string, std::to_string - HUD label assembly.
#include <vector>    // std::vector - sample buffers for procedural Wave generation.

// Project headers needed to read entity / ability / item state during draw.
// IRenderer.h has already pulled in GameState / Config / EventLog.
#include "abilities/Ability.h" // Ability::kind(), isReady(), cooldownRemaining().
#include "core/Enums.h"        // EntityKind, ItemKind, AbilityKind.
#include "entities/Enemy.h"    // Enemy::glyph(), Enemy::kind() (colour pick).
#include "entities/Player.h"   // Player::glyph(), HP, ammo, shield, charge.
#include "items/Item.h"        // Item::glyph(), Item::kind() (colour pick).

// ---------------------------------------------------------------------------
// Anonymous-namespace constants - centralises every literal the drawing code
// would otherwise hard-code (R8.5). Grouped by what they describe.
// ---------------------------------------------------------------------------
namespace {

    // ---- Window / global layout (in pixels) -------------------------------

    /// Total width of the game window.
    constexpr int WINDOW_WIDTH      = 1280;

    /// Total height of the game window.
    ///
    /// The window is taller than the strict 720p convention (720) by 40 px so
    /// the bottom controls strip can host four lines of keybinding documentation
    /// without shrinking the playable map area. Adjust here only and the rest
    /// of the layout (controls strip, HUD panel) reflows automatically.
    constexpr int WINDOW_HEIGHT     = 760;

    /// Title shown in the OS window's title bar.
    constexpr const char* WINDOW_TITLE = "Dungeon Grid Arena";

    /// Target frame rate; raylib will sleep to hold roughly this cadence.
    constexpr int TARGET_FPS        = 60;

    // ---- Map area (a 40 column x 24 row grid at 24 px per cell) -----------

    /// Side length of one dungeon cell on screen.
    constexpr int CELL_SIZE         = 24;

    /// Maximum number of grid columns we draw in the visible map area.
    /// (The actual map may be smaller; cells past this column are clipped.)
    constexpr int MAP_COLUMNS       = 40;

    /// Maximum number of grid rows we draw in the visible map area.
    constexpr int MAP_ROWS          = 24;

    /// Pixel width of the map area (40 * 24 = 960).
    constexpr int MAP_AREA_WIDTH    = MAP_COLUMNS * CELL_SIZE;

    /// Pixel height of the map area (24 * 24 = 576).
    constexpr int MAP_AREA_HEIGHT   = MAP_ROWS * CELL_SIZE;

    // ---- HUD panel (right-hand strip) -------------------------------------

    /// Left edge of the HUD panel in pixels (immediately right of the map).
    constexpr int HUD_PANEL_X       = MAP_AREA_WIDTH;

    /// Width of the HUD panel in pixels (1280 - 960 = 320).
    constexpr int HUD_PANEL_WIDTH   = WINDOW_WIDTH - MAP_AREA_WIDTH;

    /// Height of the HUD panel; spans the whole window.
    constexpr int HUD_PANEL_HEIGHT  = WINDOW_HEIGHT;

    /// Inner-padding from the panel edges to the first text/bar pixel.
    constexpr int HUD_PADDING       = 12;

    /// Vertical step between consecutive HUD lines (font height + margin).
    constexpr int HUD_LINE_HEIGHT   = 22;

    /// Pixel height of the HP and charge bars.
    constexpr int HUD_BAR_HEIGHT    = 16;

    /// Pixel height of the CHARGE bar specifically. Made taller than the HP
    /// bar so the Nova resource is unmistakable and never read as "empty"
    /// (Nova-usability feedback). Kept named so the emphasis is a single edit.
    constexpr int HUD_CHARGE_BAR_HEIGHT = 24;

    /// Thickness (pixels) of the bright outline drawn around the charge bar so
    /// it stands out as a deliberate gauge rather than blending into the panel.
    constexpr int HUD_CHARGE_BAR_OUTLINE = 2;

    /// Width of HP and charge bar in pixels (panel width minus 2*padding).
    constexpr int HUD_BAR_WIDTH     = HUD_PANEL_WIDTH - 2 * HUD_PADDING;

    // ---- Bottom controls strip (below the map) ----------------------------

    /// Top edge of the controls strip - sits just under the map.
    constexpr int CONTROLS_STRIP_Y      = MAP_AREA_HEIGHT;

    /// Height of the controls strip in pixels.
    constexpr int CONTROLS_STRIP_HEIGHT = WINDOW_HEIGHT - MAP_AREA_HEIGHT;

    // ---- Font sizes -------------------------------------------------------

    /// Font size used for the single-character entity / item glyph drawn on
    /// each cell. 18 px sits comfortably inside a 24 px cell with margin.
    constexpr int GLYPH_FONT_SIZE   = 18;

    /// Font size used for HUD text (HP / wave / ability lines).
    constexpr int HUD_FONT_SIZE     = 16;

    /// Font size used for the controls reminder under the map.
    /// Smaller than HUD text so four documented control lines fit comfortably
    /// inside the bottom strip without bumping into the map above.
    constexpr int CONTROLS_FONT_SIZE = 14;

    /// Vertical spacing between consecutive controls lines.
    constexpr int CONTROLS_LINE_HEIGHT = CONTROLS_FONT_SIZE + 6;

    /// Font size used for the LEGEND header inside the HUD panel.
    constexpr int LEGEND_HEADER_FONT_SIZE = 14;

    /// Font size used for individual LEGEND entries (kept slightly smaller
    /// than HUD text so the two-column legend fits inside the panel width).
    constexpr int LEGEND_ENTRY_FONT_SIZE = 12;

    /// Vertical spacing between consecutive LEGEND rows.
    constexpr int LEGEND_LINE_HEIGHT = LEGEND_ENTRY_FONT_SIZE + 4;

    // ---- Ability-keys panel (above the LEGEND in the HUD) -----------------
    //
    // User feedback after the first build: the bare "[1] Dash (RDY)" listing
    // didn't tell newcomers that Dash needs a direction or that Nova needs a
    // full Charge meter. The dedicated panel below spells the contract for
    // each ability key out so the player always knows what to press and
    // whether anything else is required.

    /// Font size of the "ABILITY KEYS" header above the ability-keys table.
    constexpr int ABILITY_KEYS_HEADER_FONT_SIZE = 14;

    /// One entry in the ability-keys panel: which key it is, what ability it
    /// triggers, and a short usage hint. Bundled in a struct so a future row
    /// can be added by appending to the array below without touching the
    /// rendering loop.
    struct AbilityKeyHint {
        const char* line;
    };

    /// The full ability-keys table. Each line follows the format
    /// "<key> <ability>  [<hint>]" so it reads cleanly at HUD_FONT_SIZE.
    /// The F-key (Fire) is included so the cooldown rule the player feels in
    /// game has a documented home in the HUD.
    constexpr AbilityKeyHint ABILITY_KEY_LINES[] = {
        { "1 Dash    [needs WASD direction, R=Cancel]" },
        { "2 Nova    [press alone, needs full Charge]" },
        { "3 Shield  [4-turn immunity]"             },
        { "4 Blink   [press alone, random teleport]" },
        { "F+WASD Fire [R=Cancel]"                 },
    };

    /// Number of entries in ABILITY_KEY_LINES (kept named so that adding a
    /// future row does not require chasing down a length literal).
    constexpr int ABILITY_KEY_LINE_COUNT =
        sizeof(ABILITY_KEY_LINES) / sizeof(ABILITY_KEY_LINES[0]);

    // ---- Enemy HP bar (small bar drawn above each enemy glyph) -----------
    //
    // Lets the player see at a glance who is wounded without having to read
    // an event log entry. Sized to fit comfortably inside a single CELL_SIZE
    // span without overlapping neighbouring cells.

    /// Pixel width of the small HP bar drawn above each enemy.
    /// Slightly narrower than CELL_SIZE so it sits visually inside the cell.
    constexpr int ENEMY_HP_BAR_WIDTH = CELL_SIZE - 6;

    /// Pixel height of the small HP bar above each enemy.
    constexpr int ENEMY_HP_BAR_HEIGHT = 3;

    /// Vertical offset (in pixels) from the enemy cell's top edge to the
    /// HP bar's top edge. A small negative value places the bar JUST above
    /// the cell so it does not overlap the glyph beneath.
    constexpr int ENEMY_HP_BAR_Y_OFFSET = -5;

    /// Font size used for menu entries.
    constexpr int MENU_FONT_SIZE    = 22;

    /// Font size used for stacked message lines.
    constexpr int MESSAGE_FONT_SIZE = 20;

    /// Font size for the "Fire direction?" prompt overlay.
    constexpr int FIRE_PROMPT_FONT_SIZE = 24;

    /// Font size for the "Dash direction?" prompt overlay (same as fire so
    /// both prompts feel visually equivalent and distinct only in label).
    constexpr int DASH_PROMPT_FONT_SIZE = 24;

    // ---- Layout helpers ---------------------------------------------------

    /// Pixel inset from the cell's top-left corner where a glyph is drawn.
    /// Centres the 18 px text inside the 24 px cell (24-18 = 6, /2 = 3).
    constexpr int GLYPH_INSET       = (CELL_SIZE - GLYPH_FONT_SIZE) / 2;

    /// Number of recent EventLog messages we attempt to display in the HUD.
    /// Capped to whatever the configured display capacity is.
    constexpr int HUD_EVENTS_TO_SHOW = 6;

    /// Inner width / height of the menu panel as a fraction of the window.
    constexpr int MENU_PANEL_WIDTH  = 600;

    /// Vertical line height used inside the menu overlay.
    constexpr int MENU_LINE_HEIGHT  = 30;

    /// Vertical line height used inside the message overlay.
    constexpr int MESSAGE_LINE_HEIGHT = 26;

    /// Top edge of the message overlay panel.
    constexpr int MESSAGE_PANEL_Y       = 20;

    /// Padding around the menu / message text inside their panels.
    constexpr int OVERLAY_PADDING       = 16;

    // ---- HP bar colour thresholds (percentage of max HP) ------------------

    /// At or above this fraction the HP bar is green (healthy).
    constexpr float HP_HEALTHY_THRESHOLD = 0.66f;

    /// At or above this fraction the HP bar is yellow (caution); below it red.
    constexpr float HP_CAUTION_THRESHOLD = 0.33f;

    // ---- Colour palette --------------------------------------------------
    //
    // Centralised here so the scheme is one edit away. raylib's Color literal
    // uses {r, g, b, a} with each component in [0, 255]. The opacity is 255
    // (fully opaque) for everything except the menu / message panel backgrounds
    // which use a slight transparency so the game frame remains hinted at.

    // Walls and floors have TWO palettes: a calm "neutral dungeon" baseline
    // (dark gray) used during the first 8 seconds of every wave, and a
    // dramatic "bloody dungeon" palette swapped in once hell mode triggers.
    // The hell palette is dramatically more saturated/red so the mode shift
    // is unmistakable; pixel-level brick highlights are also gated on hell
    // mode (see renderGameFrame).
    constexpr Color COLOR_WALL                = {  60,  60,  70, 255 }; // dark gray (calm)
    constexpr Color COLOR_FLOOR               = {  30,  30,  35, 255 }; // very dark (calm)
    constexpr Color COLOR_WALL_HELL           = { 110,  20,  20, 255 }; // bloody brick base
    constexpr Color COLOR_WALL_HELL_HIGHLIGHT = { 170,  40,  40, 255 }; // brick top-edge
    constexpr Color COLOR_WALL_HELL_SHADOW    = {  55,  10,  10, 255 }; // brick bottom-edge
    constexpr Color COLOR_FLOOR_HELL          = {  28,  18,  20, 255 }; // dark stone
    constexpr Color COLOR_FLOOR_HELL_STAIN    = {  55,  20,  22, 255 }; // faint blood stain
    constexpr Color COLOR_PLAYER    = {  80, 180, 255, 255 }; // bright blue
    constexpr Color COLOR_MELEE     = { 220,  60,  60, 255 }; // red 'M'
    constexpr Color COLOR_ROOK      = { 230, 130,  50, 255 }; // orange 'R'
    constexpr Color COLOR_BISHOP    = { 180,  80, 220, 255 }; // purple 'B'
    constexpr Color COLOR_QUEEN     = { 255, 210,   0, 255 }; // gold 'Q'
    constexpr Color COLOR_FAST      = { 255, 100, 100, 255 }; // light red 'F'
    constexpr Color COLOR_BOSS      = { 255,  30,  30, 255 }; // bright red 'X'
    constexpr Color COLOR_POTION    = {  60, 200, 100, 255 }; // green '!'
    constexpr Color COLOR_WEAPON    = { 240, 200,  60, 255 }; // yellow '/'
    constexpr Color COLOR_AMMO      = { 200, 200, 200, 255 }; // silver
    constexpr Color COLOR_ARMOR     = { 160, 200, 255, 255 }; // light blue
    constexpr Color COLOR_TREASURE  = { 255, 215,   0, 255 }; // gold

    constexpr Color COLOR_HUD_BG    = {  20,  20,  25, 255 }; // HUD background
    constexpr Color COLOR_HUD_BORDER= {  50,  50,  60, 255 }; // panel separator
    constexpr Color COLOR_TEXT      = { 240, 240, 240, 255 }; // primary text
    constexpr Color COLOR_TEXT_DIM  = { 160, 160, 170, 255 }; // secondary text
    constexpr Color COLOR_TEXT_HI   = { 255, 230, 120, 255 }; // selected menu
    constexpr Color COLOR_ABILITY_HEADER = { 255, 170,  60, 255 }; // orange ABILITY KEYS header

    constexpr Color COLOR_BAR_BG    = {  50,  50,  55, 255 }; // bar empty fill
    constexpr Color COLOR_HP_HIGH   = {  60, 200, 100, 255 }; // HP green
    constexpr Color COLOR_HP_MID    = { 230, 200,  60, 255 }; // HP yellow
    constexpr Color COLOR_HP_LOW    = { 220,  60,  60, 255 }; // HP red
    constexpr Color COLOR_CHARGE    = { 180,  80, 220, 255 }; // charge purple

    // ---- Fire-cooldown bar colours (Fix 1) -------------------------------
    //
    // The Fire cooldown gets its own bar (like HP / charge) so the player can
    // see at a glance whether Fire is ready. When ready the bar is fully filled
    // green; while cooling down it fills proportionally as the cooldown counts
    // back to zero, drawn in orange/red so "not ready yet" is unmistakable.

    /// Green fill + label colour shown when Fire is READY (cooldown == 0).
    constexpr Color COLOR_FIRE_READY    = {  60, 200, 100, 255 };

    /// Orange/red fill + label colour shown while the Fire cooldown ticks down.
    constexpr Color COLOR_FIRE_COOLDOWN = { 235, 110,  50, 255 };

    // ---- Charge-bar emphasis colours (Nova usability) --------------------
    //
    // The plain purple fill on a dark panel was easy to mistake for an empty
    // bar, so the charge gauge now gets a darker dedicated empty background
    // (more contrast against the purple fill), a bright outline, and — when
    // FULL — a pulsing bright fill plus a vivid "ready" label so the player
    // cannot miss that Nova is available.

    /// Darker dedicated empty background for the charge bar, chosen so the
    /// purple fill reads clearly even when the bar is only a sliver full.
    constexpr Color COLOR_CHARGE_BG       = {  28,  20,  34, 255 };

    /// Bright outline around the charge bar (lavender) so the gauge's full
    /// extent is always visible against the HUD panel.
    constexpr Color COLOR_CHARGE_OUTLINE  = { 200, 140, 255, 255 };

    /// Vivid magenta fill used when the charge bar is FULL (Nova ready). The
    /// renderer pulses between this and COLOR_CHARGE_FULL_PULSE for emphasis.
    constexpr Color COLOR_CHARGE_FULL     = { 255,  80, 255, 255 };

    /// Brighter near-white-magenta the full charge bar pulses TO, so a ready
    /// Nova visibly shimmers rather than sitting static.
    constexpr Color COLOR_CHARGE_FULL_PULSE = { 255, 200, 255, 255 };

    /// Bright yellow label colour for the "CHG: FULL — press 2 for NOVA!"
    /// callout, chosen to pop against the dark HUD background.
    constexpr Color COLOR_CHARGE_FULL_TEXT  = { 255, 240,  60, 255 };

    /// Highlight colour for the Nova entry in the abilities list when it is
    /// usable (charge full): a vivid magenta matching the full charge fill.
    constexpr Color COLOR_NOVA_READY_TEXT   = { 255, 110, 255, 255 };

    /// Speed (radians per second) at which the FULL charge bar pulses between
    /// its two emphasis colours. Tuned for a calm but noticeable shimmer.
    constexpr float CHARGE_PULSE_SPEED = 6.0f;

    /// Opaque-ish overlay panel for menu / message backgrounds.
    constexpr Color COLOR_OVERLAY_PANEL = { 15, 15, 20, 230 };

    // ---- Fire effect colours / timing ------------------------------------
    //
    // showFireEffect captures the cells the projectile passed through; the
    // next ~half second of frames render a tracer line through the trail and
    // a brighter flash on the impact cell. The tracer colour AND the impact
    // colour both differ between hit and miss so the player can read the
    // outcome at a glance:
    //   * HIT  → bright RED tracer + RED+yellow flash (clear "you struck").
    //   * MISS → dull GRAY/SMOKE tracer + GRAY puff (clear "you whiffed").
    // The frame count is named so balancing the persistence is a single edit.

    /// Bright red tracer for a HIT shot — same hue as the impact flash so the
    /// trail and the explosion read as one continuous "this struck" cue.
    constexpr Color COLOR_FIRE_TRAIL_HIT = { 255,  90,  60, 230 };

    /// Dull light gray tracer for a MISS shot — deliberately desaturated so
    /// it reads as smoke/dust rather than the bright red of a successful hit.
    constexpr Color COLOR_FIRE_TRAIL_MISS = { 170, 170, 175, 180 };

    /// Bright red core flash drawn under the impact cell on a hit; combined
    /// with COLOR_FIRE_IMPACT_HIT_FLASH below to produce a vivid red+yellow
    /// burst that contrasts strongly with miss puffs.
    constexpr Color COLOR_FIRE_IMPACT_HIT  = { 255,  50,  50, 240 };

    /// Bright yellow accent flash drawn on top of COLOR_FIRE_IMPACT_HIT to
    /// give the impact cell a hot core (the visual reward for a clean shot).
    constexpr Color COLOR_FIRE_IMPACT_HIT_FLASH = { 255, 230, 120, 220 };

    /// Dull gray puff for a MISS impact — desaturated and dim so a missed
    /// shot is unmistakably less satisfying than a hit.
    constexpr Color COLOR_FIRE_IMPACT_MISS = { 130, 130, 135, 200 };

    /// How many rendered frames the fire effect persists. At 60 fps this is
    /// roughly half a second, long enough to read a tracer and short enough
    /// not to clutter the next turn.
    constexpr int FIRE_EFFECT_FRAMES = 30;

    /// Pixel thickness of the trail tracer line — kept named so HIT and MISS
    /// trails always agree on width even when their colours differ.
    constexpr float FIRE_TRAIL_THICKNESS = 3.0f;

    /// Radius of the inner yellow core drawn over the red impact square on a
    /// successful hit, in pixels. Tuned to sit comfortably inside one cell.
    constexpr int FIRE_IMPACT_HIT_CORE_RADIUS = (CELL_SIZE / 2) - 4;

    // ---- Nova ultimate shockwave effect (Fix 2) --------------------------
    //
    // showNovaEffect records the blast centre + radius; the next
    // NOVA_EFFECT_FRAMES rendered frames draw an EXPANDING shockwave centred on
    // the player: several concentric rings that grow with the animation, a
    // translucent filled blast disc covering the radius, and a few jagged
    // electric-arc lines from the centre to the edge for an electric look. Every
    // tunable lives here so the drama is a single-edit balance (R8.5).

    /// How many rendered frames the Nova effect persists. At 60 fps this is a
    /// little over half a second — long enough to read as a dramatic ultimate
    /// without lingering into the next turn.
    constexpr int NOVA_EFFECT_FRAMES = 35;

    /// Number of concentric rings drawn in the expanding shockwave. More rings
    /// read as a denser, flashier blast.
    constexpr int NOVA_RING_COUNT = 4;

    /// Pixel thickness of each expanding shockwave ring.
    constexpr float NOVA_RING_THICKNESS = 3.0f;

    /// Number of jagged electric-arc lines radiated from the blast centre to the
    /// edge for the "electric" look.
    constexpr int NOVA_ARC_COUNT = 12;

    /// Number of small segments each electric arc is broken into so it can zig-
    /// zag (a single straight line would not read as electric).
    constexpr int NOVA_ARC_SEGMENTS = 4;

    /// Maximum pixel jitter applied perpendicular to each arc segment so the
    /// arcs look jagged rather than straight.
    constexpr int NOVA_ARC_JITTER = 6;

    /// Pixel thickness of each electric-arc line.
    constexpr float NOVA_ARC_THICKNESS = 2.0f;

    /// Bright cyan used for the outermost / leading shockwave ring.
    constexpr Color COLOR_NOVA_RING_CYAN  = {  80, 220, 255, 230 };

    /// Electric blue used for the middle shockwave rings.
    constexpr Color COLOR_NOVA_RING_BLUE  = {  60, 130, 255, 220 };

    /// Near-white core flash used for the innermost ring and the arc lines, so
    /// the centre of the blast reads as a hot electric burst.
    constexpr Color COLOR_NOVA_WHITE      = { 230, 245, 255, 240 };

    /// Translucent fill of the blast disc covering the whole radius, low alpha
    /// so the floor / glyphs beneath remain visible through the flash.
    constexpr Color COLOR_NOVA_BLAST_FILL = { 120, 200, 255,  70 };

    // ---- Transient on-screen notice (Fix 1) ------------------------------
    //
    // showTransientNotice posts a short message that renderGameFrame draws near
    // the bottom-centre of the window for NOTICE_EFFECT_FRAMES frames, giving the
    // player immediate feedback when the UI refuses an action (e.g. pressing F
    // while the Fire cooldown is still ticking).

    // ---- Player melee effect (Fix 4) -------------------------------------
    //
    // showPlayerMeleeEffect records the target cell; the next few rendered frames
    // draw a bright red "X" (two crossing diagonal lines) centred on that cell so
    // the player sees their melee attack land.

    /// How many rendered frames the player melee effect persists. At 60 fps this
    /// is roughly a third of a second — long enough to notice the strike landed,
    /// short enough not to linger into the next turn.
    constexpr int MELEE_EFFECT_FRAMES = 20;

    /// Bright red colour of the player melee "X" slash — matches the colour used
    /// for enemy melee flashes so both directions of melee read as the same kind
    /// of attack.
    constexpr Color COLOR_PLAYER_MELEE_SLASH = { 255, 50, 50, 240 };

    /// Pixel thickness of each of the two diagonal slash lines.
    constexpr float MELEE_SLASH_THICKNESS = 3.0f;

    /// Inset (pixels) from the cell edges to the endpoints of the slash lines.
    /// Keeps the "X" visually inside the cell rather than overlapping neighbours.
    constexpr int MELEE_SLASH_INSET = 4;

    /// How many rendered frames a transient notice stays on screen (~2/3 s at
    /// 60 fps) — long enough to read, short enough not to nag.
    constexpr int NOTICE_EFFECT_FRAMES = 40;

    /// Font size of the transient notice text.
    constexpr int NOTICE_FONT_SIZE = 22;

    /// Padding inside the transient-notice panel around its text.
    constexpr int NOTICE_PADDING = 10;

    /// Pixel gap between the bottom of the notice panel and the top of the
    /// controls strip, so the notice floats just above the controls.
    constexpr int NOTICE_BOTTOM_MARGIN = 8;

    /// Bright orange/amber text for the notice, chosen to pop as a warning.
    constexpr Color COLOR_NOTICE_TEXT  = { 255, 200,  70, 255 };

    /// Dark semi-opaque panel behind the notice text for legibility.
    constexpr Color COLOR_NOTICE_PANEL = {  20,  15,  10, 230 };

    // ---- Enemy-attack effect colours / timing ----------------------------
    //
    // showEnemyAttackEffect records each enemy hit on the player; the next
    // ~third of a second of frames draw a cue so the player understands they
    // are under attack and from where:
    //   * RANGED (Rook/Bishop/Queen/Boss) → an orange beam from the attacker's
    //     tile to the player's tile, capped with a small burst at the player.
    //   * MELEE  (Melee/Fast)             → a translucent red slash flash drawn
    //     over the player's own tile.
    // The frame count is named so the persistence is a single-edit balance.

    /// Orange beam colour for a ranged enemy attack (Rook/Bishop/Queen/Boss).
    /// Distinct from the player's red/gray fire tracer so the player can tell
    /// "incoming" beams apart from their own shots. Used for a HIT (the shot
    /// connected and dealt damage).
    constexpr Color COLOR_ENEMY_BEAM = { 255, 140, 30, 220 };

    /// Dim, desaturated orange beam for a ranged enemy MISS (Feature 3). Drawn
    /// instead of COLOR_ENEMY_BEAM when the distance roll failed, so the player
    /// sees the shot go out but can read that it whiffed (lower alpha + duller
    /// hue than the bright hit beam).
    constexpr Color COLOR_ENEMY_BEAM_MISS = { 200, 130, 60, 110 };

    /// Translucent red flash drawn over the player's tile for a melee hit, so
    /// an adjacent contact attack reads as a sharp red slash on the hero.
    constexpr Color COLOR_ENEMY_MELEE_FLASH = { 230, 40, 40, 150 };

    /// Small burst circle drawn at the player end of a ranged beam to mark the
    /// point of impact (same hue as the beam for a cohesive cue).
    constexpr Color COLOR_ENEMY_BEAM_IMPACT = { 255, 90, 30, 220 };

    /// How many rendered frames an enemy-attack cue persists. At 60 fps this is
    /// roughly a third of a second — long enough to notice, short enough not to
    /// linger into the next turn.
    constexpr int ENEMY_ATTACK_EFFECT_FRAMES = 20;

    /// Pixel thickness of the ranged-attack beam line.
    constexpr float ENEMY_BEAM_THICKNESS = 3.0f;

    /// Radius (pixels) of the burst circle at the player end of a ranged beam.
    constexpr int ENEMY_BEAM_IMPACT_RADIUS = (CELL_SIZE / 2) - 5;

    // ---- Fire-aim preview (Feature 2) -------------------------------------
    //
    // While the player is choosing a fire direction the renderer overlays the
    // four firing lanes on the map: each reachable cell up to the player's fire
    // range is painted a semi-transparent yellow/orange, and the FIRST enemy in
    // each lane is marked red (it would be hit). This lets the player see how
    // far each shot reaches and which targets are in range before committing.

    /// Semi-transparent yellow/orange fill drawn on each cell a shot could
    /// travel through. Low alpha so the floor / glyph beneath stays readable.
    constexpr Color COLOR_FIRE_PREVIEW_LANE = { 255, 200, 60, 90 };

    /// Brighter red fill marking the first enemy in a lane (the cell that would
    /// be hit if the player fires that direction).
    constexpr Color COLOR_FIRE_PREVIEW_TARGET = { 255, 60, 60, 150 };

    /// Inset (pixels) of the small preview square inside its cell, so adjacent
    /// lane cells read as separate pips rather than one solid band.
    constexpr int FIRE_PREVIEW_INSET = 5;

    // ---- Player highlight (R-friendly UX: the hero must be unmistakable) --
    //
    // To make the player obvious at a glance the renderer draws a bright
    // cyan/blue circle behind the @ glyph plus a soft larger glow ring at a
    // small radius. The two values let the highlight be retuned in one place.

    /// Radius (in pixels) of the solid disc drawn behind the player glyph.
    /// Sized to fit comfortably inside one cell with a couple of pixels of
    /// margin around it.
    constexpr int PLAYER_HIGHLIGHT_RADIUS = (CELL_SIZE / 2) - 2;

    /// Radius (in pixels) of the soft outer glow ring centred on the player.
    /// Roughly two cells wide so the player's location stands out even when
    /// surrounded by a cluster of enemy glyphs.
    constexpr int PLAYER_GLOW_RADIUS = CELL_SIZE * 2;

    /// Cyan / blue accent used for the solid disc behind the player.
    constexpr Color COLOR_PLAYER_DISC  = {  60, 200, 255, 220 };

    /// Soft translucent ring drawn around the player as a glow accent. The
    /// low alpha lets the underlying floor and items remain visible.
    constexpr Color COLOR_PLAYER_GLOW  = { 100, 200, 255,  35 };

    /// Outline colour around the player cell (a thicker static border in
    /// place of a true pulsing animation, so the player is easy to spot at a
    /// glance without taxing the renderer).
    constexpr Color COLOR_PLAYER_BORDER= { 200, 240, 255, 255 };

    /// Pixel thickness of the static player-cell border.
    constexpr int PLAYER_BORDER_THICKNESS = 2;

    /// Multi-line controls reminder shown under the map in a structured
    /// breakdown (one row per category). User feedback after the first build
    /// said the previous single-line strip was hard to skim; spelling each
    /// binding out solves that without growing the window much.
    constexpr const char* CONTROLS_LINES[] = {
        "MOVEMENT:  WASD or Arrow Keys",
        "COMBAT:    F + Direction = Fire     Walk into enemy = Melee attack",
        "ABILITIES: 1=Dash   2=Nova(needs full Charge)   3=Shield   4=Blink",
        "SYSTEM:    Space=Wait/Confirm    Q=Quit to menu    Ctrl+S=Save game",
    };

    /// Number of entries in CONTROLS_LINES (kept named so a future row added
    /// to the array does not require touching the loop that prints them).
    constexpr int CONTROLS_LINE_COUNT =
        sizeof(CONTROLS_LINES) / sizeof(CONTROLS_LINES[0]);

    // ---- Audio constants (procedural SFX + ambient music) ----------------
    //
    // Every audio knob lives here so balancing the mix or retuning a synth
    // patch is a single-edit change (R8.5). Sample rate, bit depth, and
    // channel count are chosen to match raylib's audio module's preferred
    // 16-bit signed PCM at 44.1 kHz.

    /// Sample rate used for every procedural Wave and the ambient music
    /// stream. 44.1 kHz is the standard "CD quality" rate raylib expects.
    constexpr int AUDIO_SAMPLE_RATE = 44100;

    /// Bit depth used for procedural sample buffers — 16-bit signed integer
    /// PCM, which is what raylib's PlaySound and UpdateAudioStream consume by
    /// default.
    constexpr int AUDIO_SAMPLE_SIZE = 16;

    /// Number of channels for the SFX buffers — mono is enough; stereo would
    /// only double the memory without adding any positional information.
    constexpr int AUDIO_SFX_CHANNELS = 1;

    /// Number of channels for the ambient music stream — stereo so the slow
    /// detuned drone fills both speakers and feels spacious.
    constexpr int AUDIO_MUSIC_CHANNELS = 2;

    /// Peak amplitude of one PCM sample in the int16 range (32767). Used to
    /// scale floating-point synth output before storing as int16.
    constexpr float AUDIO_INT16_PEAK = 32767.0f;

    /// Master volume applied to every procedural sound (in [0, 1]). Kept
    /// slightly below 1 so simultaneous SFX never clip the output bus.
    constexpr float AUDIO_SFX_VOLUME = 0.7f;

    /// Master volume of the ambient drone (in [0, 1]). Deliberately low so the
    /// background never drowns out gameplay sounds. Pulled down further from
    /// the original 0.30 because the previous drone read as too prominent —
    /// the new pad sits underneath the SFX without ever competing with them.
    constexpr float AUDIO_MUSIC_VOLUME = 0.16f;

    /// Filesystem location of the streamed boss-wave background track. Loaded
    /// once at construction via LoadMusicStream; if the file is missing or the
    /// decoder rejects the format, the renderer silently degrades to no boss
    /// music (every later setBossMusicActive call becomes a no-op).
    constexpr const char* BOSS_MUSIC_PATH = "assets/boss_theme.ogg";

    /// Filesystem location of the streamed NORMAL-wave background track (the
    /// high-energy "action" theme that plays during every non-boss wave). Same
    /// load semantics as BOSS_MUSIC_PATH — a missing file degrades to silence.
    constexpr const char* NORMAL_MUSIC_PATH = "assets/normal_theme.ogg";

    /// Master volume of the boss-wave track. Pushed a touch higher than the
    /// ambient drone so the boss fight feels more intense, while still leaving
    /// headroom for SFX (fire, melee, ability casts) to be heard over it.
    constexpr float BOSS_MUSIC_VOLUME = 0.55f;

    /// Volume the ambient procedural drone is reduced to while the boss track
    /// is playing. Not muted entirely so the layered low end keeps the room
    /// feeling tense; switched back to AUDIO_MUSIC_VOLUME when the boss wave
    /// ends.
    constexpr float AUDIO_MUSIC_VOLUME_DUCKED = 0.04f;

    /// Tau (2*pi) — used by every sine-based synth helper.
    constexpr float AUDIO_TWO_PI = 6.28318530717958647692f;

    // ---- Per-effect synth parameters -------------------------------------

    /// Player Fire — a sharp shotgun-style noise burst with a short
    /// descending tone tail. Heavier noise level + a touch more duration
    /// makes the gun read as a real weapon rather than a chip-tune blip.
    constexpr float FIRE_SOUND_DURATION_SEC      = 0.22f;
    constexpr float FIRE_SOUND_FREQ_START_HZ     = 900.0f;
    constexpr float FIRE_SOUND_FREQ_END_HZ       = 110.0f;
    constexpr float FIRE_SOUND_NOISE_LEVEL       = 0.85f;
    constexpr float FIRE_SOUND_TONE_LEVEL        = 0.45f;

    /// Player Fire IMPACT on enemy — a deep, longer, low-pitched thud that
    /// lands AFTER the gun shot so a successful hit feels weighty.
    constexpr float HIT_SOUND_DURATION_SEC       = 0.22f;
    constexpr float HIT_SOUND_FREQ_HZ            = 90.0f;
    constexpr float HIT_SOUND_LEVEL              = 0.95f;

    /// Player melee — heavier noise component (slash + bone crunch) and a
    /// deeper tone underneath. Slightly longer than before so it does not
    /// feel like a click.
    constexpr float MELEE_SOUND_DURATION_SEC     = 0.20f;
    constexpr float MELEE_SOUND_FREQ_HZ          = 120.0f;
    constexpr float MELEE_SOUND_NOISE_LEVEL      = 0.85f;
    constexpr float MELEE_SOUND_TONE_LEVEL       = 0.40f;

    /// Enemy attack lands on player — a deeper, longer thud than HIT_SOUND so
    /// the player INSTANTLY hears that they were the one struck (lower pitch
    /// = bigger threat).
    constexpr float ENEMY_HIT_SOUND_DURATION_SEC = 0.28f;
    constexpr float ENEMY_HIT_SOUND_FREQ_HZ      = 75.0f;
    constexpr float ENEMY_HIT_SOUND_LEVEL        = 1.00f;

    constexpr float NOVA_SOUND_DURATION_SEC      = 0.80f;
    constexpr float NOVA_SOUND_FREQ_START_HZ     = 1800.0f;
    constexpr float NOVA_SOUND_FREQ_END_HZ       = 60.0f;
    constexpr float NOVA_SOUND_NOISE_LEVEL       = 0.55f;
    constexpr float NOVA_SOUND_TONE_LEVEL        = 0.45f;

    /// Ascending major chord (C5, E5, G5) used for the wave-clear stinger.
    /// 523.25 / 659.25 / 783.99 Hz are the standard equal-temperament tunings.
    constexpr float WAVE_CLEAR_DURATION_SEC      = 0.50f;
    constexpr float WAVE_CLEAR_LEVEL             = 0.6f;
    constexpr float WAVE_CLEAR_FREQS_HZ[]        = { 523.25f, 659.25f, 783.99f };
    constexpr int   WAVE_CLEAR_FREQ_COUNT        =
        static_cast<int>(sizeof(WAVE_CLEAR_FREQS_HZ) / sizeof(WAVE_CLEAR_FREQS_HZ[0]));

    /// Descending minor chord (G4, Eb4, C4) for the game-over sting.
    constexpr float GAME_OVER_DURATION_SEC       = 0.80f;
    constexpr float GAME_OVER_LEVEL              = 0.55f;
    constexpr float GAME_OVER_FREQS_HZ[]         = { 392.00f, 311.13f, 261.63f };
    constexpr int   GAME_OVER_FREQ_COUNT         =
        static_cast<int>(sizeof(GAME_OVER_FREQS_HZ) / sizeof(GAME_OVER_FREQS_HZ[0]));

    constexpr float PICKUP_DURATION_SEC          = 0.20f;
    constexpr float PICKUP_LEVEL                 = 0.55f;
    /// Two ascending sine notes (C5 → G5) blended within one buffer.
    constexpr float PICKUP_FREQ_LOW_HZ           = 523.25f;
    constexpr float PICKUP_FREQ_HIGH_HZ          = 783.99f;

    constexpr float DASH_SOUND_DURATION_SEC      = 0.20f;
    constexpr float DASH_SOUND_FREQ_START_HZ     = 200.0f;
    constexpr float DASH_SOUND_FREQ_END_HZ       = 800.0f;
    constexpr float DASH_SOUND_NOISE_LEVEL       = 0.55f;
    constexpr float DASH_SOUND_TONE_LEVEL        = 0.25f;

    /// Shield: sustained shimmering chord (A4, E5).
    constexpr float SHIELD_SOUND_DURATION_SEC    = 0.30f;
    constexpr float SHIELD_SOUND_LEVEL           = 0.5f;
    constexpr float SHIELD_FREQS_HZ[]            = { 440.00f, 659.25f };
    constexpr int   SHIELD_FREQ_COUNT            =
        static_cast<int>(sizeof(SHIELD_FREQS_HZ) / sizeof(SHIELD_FREQS_HZ[0]));

    constexpr float BLINK_SOUND_DURATION_SEC     = 0.20f;
    constexpr float BLINK_SOUND_FREQ_START_HZ    = 2000.0f;
    constexpr float BLINK_SOUND_FREQ_END_HZ      = 400.0f;
    constexpr float BLINK_SOUND_NOISE_LEVEL      = 0.20f;
    constexpr float BLINK_SOUND_TONE_LEVEL       = 0.65f;

    /// Simple lowpass cutoff (in [0, 1]) used by makeNoiseBurst — the running
    /// average's blend factor. Lower values = more muffled noise.
    constexpr float NOISE_LOWPASS_DEFAULT        = 0.35f;

    // ---- Ambient music (calm pad) ----------------------------------------
    //
    // The original ambient layer was just two slow detuned sines + an LFO and
    // it ended up sounding more like a refrigerator hum than a soundtrack. The
    // replacement is a slow A-minor pad: four sine voices arranged as a wide
    // chord (A2 root, E3 perfect fifth, C4 minor third, A3 octave) with a very
    // gentle slow tremolo. Sine waves keep CPU cost negligible on the audio
    // thread; the chord shape is what carries the "music" feeling.

    /// Number of voices in the ambient pad chord. Kept named so that adding
    /// or removing a voice is a single-edit change and the per-voice mix gain
    /// rebalances itself automatically.
    constexpr int   MUSIC_PAD_VOICE_COUNT = 4;

    /// Pad voice frequencies (Hz). The chord is A minor:
    ///     index 0 — A2 (~110 Hz)  : root, low foundation.
    ///     index 1 — E3 (~165 Hz)  : perfect fifth, fills out the bottom.
    ///     index 2 — C4 (~262 Hz)  : minor third, gives the chord its mood.
    ///     index 3 — A3 (~220 Hz)  : root one octave up, brightens the top.
    /// Choosing nearby (but not identical) frequencies for two of the voices
    /// adds a faint chorus shimmer without sounding dissonant.
    constexpr float MUSIC_PAD_FREQS_HZ[MUSIC_PAD_VOICE_COUNT] = {
        110.00f, 164.81f, 261.63f, 220.00f
    };

    /// LFO rate (Hz) for the slow amplitude swell of the pad — about one
    /// breath every ~12 seconds (frequency ≈ 0.083 Hz). Slower than the old
    /// drone's LFO so the pad feels more like ambient music and less like a
    /// pulsing hum.
    constexpr float MUSIC_LFO_RATE_HZ        = 0.083f;

    /// LFO depth: the LFO modulates the amplitude between (1 - depth) and 1.
    /// 0.30 keeps the pad audible at its quietest moments while still moving.
    constexpr float MUSIC_LFO_DEPTH          = 0.30f;

    /// Per-voice mix gain (in [0, 1]) before the AUDIO_MUSIC_VOLUME stream
    /// volume is applied. With four voices summed, 0.30 keeps the peak below
    /// clipping while staying loud enough to read as a layered chord.
    constexpr float MUSIC_VOICE_LEVEL        = 0.30f;

    // ---- Hell-mode visual overlay ----------------------------------------
    //
    /// Once a wave has been live for this many real-time seconds, the renderer
    /// flips into "Death Dungeon" mode (red vignette + ember particles +
    /// pulsing banner + bloody-brick wall reskin + villain laugh sting).
    /// Tuned to ~9.5 seconds for normal waves — long enough that the start of
    /// a wave feels normal and quiet, short enough that the player gets the
    /// dramatic mode shift well before the wave is finished.
    constexpr double HELL_MODE_TRIGGER_SECONDS_NORMAL = 9.5;

    /// Boss-wave trigger threshold. Pushed out to ~19 seconds so the boss
    /// fight has a longer build-up (the boss music and ambient pad get more
    /// breathing room before the visual escalation lands).
    constexpr double HELL_MODE_TRIGGER_SECONDS_BOSS   = 19.0;

    /// Boss-wave cadence — every fifth wave is a boss wave (5, 10, 15, ...).
    /// Mirrors WaveManager.cpp's local kBossWaveModulus and Game.cpp's
    /// BOSS_MUSIC_WAVE_MODULUS; kept here so the renderer can pick the right
    /// trigger threshold without taking a dependency on either of those
    /// modules. If the cadence ever changes this constant must change with it.
    constexpr int    HELL_MODE_BOSS_WAVE_MODULUS      = 5;

    /// Number of frames the "HELL ON EARTH" banner is drawn at the top of the
    /// map after hell mode triggers. At 60 fps this is ~2 s — visible but not
    /// hanging around long enough to obstruct gameplay.
    constexpr int    HELL_MODE_BANNER_FRAMES      = 120;

    /// Number of glowing embers in the upward-drifting particle pool. 80 fits
    /// the map area without thrashing the renderer at 60 fps.
    constexpr int    HELL_EMBER_COUNT             = 80;

    /// Minimum / maximum life (frames) for one ember before it is recycled.
    /// Values picked so an ember rises ~half the map height before respawning.
    constexpr int    HELL_EMBER_LIFE_MIN_FRAMES   = 60;
    constexpr int    HELL_EMBER_LIFE_MAX_FRAMES   = 180;

    /// Maximum vertical speed (pixels/frame) of an ember. Negative because
    /// the y-axis grows downward in raylib; embers drift UP.
    constexpr float  HELL_EMBER_VY_MAX             = 1.6f;
    constexpr float  HELL_EMBER_VY_MIN             = 0.4f;

    /// Maximum horizontal drift (pixels/frame) of an ember. Tiny so the
    /// embers don't shoot sideways; they just wobble.
    constexpr float  HELL_EMBER_VX_RANGE           = 0.5f;

    /// Pixel radius of an ember's glowing core. Embers also draw an outer
    /// translucent ring at twice this radius for the soft glow.
    constexpr int    HELL_EMBER_CORE_RADIUS        = 2;

    /// Vignette colour: deep red, fully transparent at the centre and ramping
    /// up at the edges so the borders of the map darken into a hellish glow.
    constexpr unsigned char HELL_VIGNETTE_R        = 180;
    constexpr unsigned char HELL_VIGNETTE_G        = 20;
    constexpr unsigned char HELL_VIGNETTE_B        = 10;

    /// Maximum alpha (0..255) of the vignette layers at the very edge of the
    /// map. Pulsed up and down by a slow sine so the screen "breathes" red.
    constexpr int    HELL_VIGNETTE_MAX_ALPHA       = 90;

    /// Banner background / text colours.
    constexpr Color  HELL_BANNER_BACKGROUND        = { 30,  0,  0, 200 };
    constexpr Color  HELL_BANNER_TEXT_COLOR        = { 255, 60, 30, 255 };
    constexpr int    HELL_BANNER_FONT_SIZE         = 36;
    constexpr int    HELL_BANNER_HEIGHT_PX         = 80;
    constexpr const char* HELL_BANNER_TEXT         = "DEATH DUNGEON";

} // anonymous namespace

namespace dga {

// =============================================================================
// Internal helpers (file-local) - colour selection from entity / item kind
// =============================================================================

namespace {

    /// Map an EntityKind to the renderer's signature colour for that creature.
    /// Wrapped in a function (rather than a lookup table) so a future kind not
    /// in the enum still yields a sensible default rather than reading garbage.
    /// @param kind the EntityKind to translate to a Color.
    /// @return the palette colour to draw an entity of `kind` with.
    Color entityColorFor(EntityKind kind)
    {
        switch (kind) {
            case EntityKind::Player:      return COLOR_PLAYER;
            case EntityKind::MeleeEnemy:  return COLOR_MELEE;
            case EntityKind::RookEnemy:   return COLOR_ROOK;
            case EntityKind::BishopEnemy: return COLOR_BISHOP;
            case EntityKind::QueenEnemy:  return COLOR_QUEEN;
            case EntityKind::FastEnemy:   return COLOR_FAST;
            case EntityKind::BossEnemy:   return COLOR_BOSS;
        }
        return COLOR_TEXT; // Defensive: unknown kind falls back to plain text.
    }

    /// Map an ItemKind to the renderer's signature colour for that pickup.
    /// @param kind the ItemKind to translate to a Color.
    /// @return the palette colour to draw an item of `kind` with.
    Color itemColorFor(ItemKind kind)
    {
        switch (kind) {
            case ItemKind::HealthPotion: return COLOR_POTION;
            case ItemKind::Weapon:       return COLOR_WEAPON;
            case ItemKind::AmmoItem:     return COLOR_AMMO;
            case ItemKind::Armor:        return COLOR_ARMOR;
            case ItemKind::Treasure:     return COLOR_TREASURE;
        }
        return COLOR_TEXT;
    }

    /// Choose the HP bar fill colour from the current HP percentage.
    /// >= HP_HEALTHY_THRESHOLD -> green, >= HP_CAUTION_THRESHOLD -> yellow,
    /// else red. Pure decision helper, no side effects.
    /// @param healthRatio the player's current HP divided by max HP, in [0,1].
    /// @return the palette colour to fill the HP bar with.
    Color hpBarColorFor(float healthRatio)
    {
        if (healthRatio >= HP_HEALTHY_THRESHOLD) { return COLOR_HP_HIGH; }
        if (healthRatio >= HP_CAUTION_THRESHOLD) { return COLOR_HP_MID;  }
        return COLOR_HP_LOW;
    }

    /// Translate an AbilityKind into a short HUD label.
    /// @param kind the AbilityKind to label.
    /// @return a stable short string used in the abilities list.
    const char* abilityLabel(AbilityKind kind)
    {
        switch (kind) {
            case AbilityKind::Dash:   return "[1] Dash";
            case AbilityKind::Nova:   return "[2] Nova";
            case AbilityKind::Shield: return "[3] Shield";
            case AbilityKind::Blink:  return "[4] Blink";
        }
        return "[?] ?";
    }

    /// Linearly interpolate between two raylib colours, component-wise.
    /// Used to pulse the FULL charge bar between its base and bright magenta
    /// so a ready Nova visibly shimmers. The blend factor is clamped to [0,1]
    /// so an out-of-range t can never produce an invalid colour component.
    /// @param a colour returned when t == 0.
    /// @param b colour returned when t == 1.
    /// @param t blend factor in [0,1].
    /// @return the interpolated colour.
    Color lerpColor(Color a, Color b, float t)
    {
        const float clamped = std::clamp(t, 0.0f, 1.0f);
        const auto mix = [clamped](unsigned char ca, unsigned char cb) {
            const float v = static_cast<float>(ca) +
                            (static_cast<float>(cb) - static_cast<float>(ca)) * clamped;
            return static_cast<unsigned char>(v);
        };
        return Color{ mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a) };
    }

    // -----------------------------------------------------------------------
    // Procedural audio synthesis helpers (file-local).
    //
    // Each helper allocates an int16 sample buffer, fills it with a tiny
    // hand-rolled DSP, and hands it to LoadSoundFromWave so the caller gets a
    // ready-to-play raylib Sound back. The Wave's data pointer is allocated
    // with std::malloc because raylib's UnloadSound (and UnloadWave when used)
    // free the buffer with std::free under the hood.
    // -----------------------------------------------------------------------

    /// Convert a floating-point sample in roughly [-1, 1] to a clipped int16
    /// PCM value. Anything outside the range saturates rather than wraps.
    /// @param sample the raw sample to convert.
    /// @return the int16 PCM equivalent, clipped to [-32767, 32767].
    std::int16_t toInt16(float sample)
    {
        const float clipped = std::clamp(sample, -1.0f, 1.0f);
        return static_cast<std::int16_t>(clipped * AUDIO_INT16_PEAK);
    }

    /// Compute a short attack/release envelope (in [0, 1]) for sample index
    /// `i` of `total`. A 5 % attack and a 30 % release tail keep the synth
    /// click-free at the edges and produces a soft "fall-off" tail.
    /// @param i     current sample index.
    /// @param total total sample count of the buffer.
    /// @return the envelope value at this sample position.
    float adsrEnvelope(int i, int total)
    {
        if (total <= 1) { return 1.0f; }
        const float t = static_cast<float>(i) / static_cast<float>(total - 1);
        // 5 % attack ramping from 0 to 1.
        constexpr float ATTACK = 0.05f;
        // Last 30 % linearly fades from 1 down to 0 (release tail).
        constexpr float RELEASE_START = 0.70f;
        if (t < ATTACK) {
            return t / ATTACK;
        }
        if (t > RELEASE_START) {
            return 1.0f - (t - RELEASE_START) / (1.0f - RELEASE_START);
        }
        return 1.0f;
    }

    /// Allocate a freshly zeroed int16 sample buffer sized for `frameCount`
    /// MONO frames using std::malloc (raylib's UnloadSound expects malloc'd
    /// memory). Exits on out-of-memory by returning nullptr; the caller turns
    /// the resulting Wave into a Sound, which itself becomes a no-op silent
    /// sound.
    /// @param frameCount number of mono frames to allocate.
    /// @return malloc'd zero-initialised int16 buffer (caller does not free it
    ///         directly; UnloadSound takes ownership through the Wave).
    std::int16_t* allocSampleBuffer(int frameCount)
    {
        const std::size_t bytes = static_cast<std::size_t>(frameCount) * sizeof(std::int16_t);
        auto* data = static_cast<std::int16_t*>(std::malloc(bytes));
        if (data != nullptr) {
            std::memset(data, 0, bytes);
        }
        return data;
    }

    /// Wrap a freshly synthesised sample buffer in a raylib Wave and convert it
    /// to a Sound. Frees the temporary Wave once the Sound has been built so
    /// the only remaining ownership lives inside the returned Sound (which the
    /// renderer's destructor unloads via UnloadSound).
    /// @param data        the malloc'd int16 sample buffer (mono).
    /// @param frameCount  number of mono frames in `data`.
    /// @return a ready-to-play raylib Sound; an empty/silent Sound on failure.
    Sound waveToSound(std::int16_t* data, int frameCount)
    {
        Wave wave{};
        wave.frameCount = static_cast<unsigned int>(frameCount);
        wave.sampleRate = static_cast<unsigned int>(AUDIO_SAMPLE_RATE);
        wave.sampleSize = static_cast<unsigned int>(AUDIO_SAMPLE_SIZE);
        wave.channels   = static_cast<unsigned int>(AUDIO_SFX_CHANNELS);
        wave.data       = data;
        // LoadSoundFromWave copies the samples into the audio driver's owned
        // buffer, so we are free to drop the Wave once the Sound exists.
        Sound sound = LoadSoundFromWave(wave);
        UnloadWave(wave);
        return sound;
    }

    /// Build a Sound that sweeps from `freqStart` to `freqEnd` (Hz) over
    /// `durationSec` seconds, scaled by `volume` (in [0, 1]). The tone uses a
    /// soft attack/release envelope so it never clicks. Used for the various
    /// "tone" components (fire descending whistle, blink zap, etc.).
    Sound makeBeepSound(float freqStart, float freqEnd,
                        float durationSec, float volume)
    {
        const int frameCount = static_cast<int>(
            durationSec * static_cast<float>(AUDIO_SAMPLE_RATE));
        std::int16_t* data = allocSampleBuffer(frameCount);
        if (data == nullptr) { return Sound{}; }

        // Phase accumulator: integrating the (potentially-changing) frequency
        // each sample avoids any audible "click" from discontinuous phase.
        float phase = 0.0f;
        for (int i = 0; i < frameCount; ++i) {
            const float t = static_cast<float>(i) /
                            static_cast<float>(frameCount > 1 ? frameCount - 1 : 1);
            const float freq = freqStart + (freqEnd - freqStart) * t;
            phase += AUDIO_TWO_PI * freq /
                     static_cast<float>(AUDIO_SAMPLE_RATE);
            const float envelope = adsrEnvelope(i, frameCount);
            const float sample   = std::sin(phase) * envelope * volume;
            data[i] = toInt16(sample);
        }
        return waveToSound(data, frameCount);
    }

    /// Build a Sound consisting of white noise (rand()) passed through a simple
    /// one-pole lowpass (running average). The cutoff is the lowpass blend
    /// factor in [0, 1]; lower values produce duller, more muffled noise.
    /// Useful for whooshes and impacts.
    /// Marked [[maybe_unused]] because the current sound design uses
    /// makeNoiseAndTone (noise+tone hybrid) instead — this helper stays in the
    /// toolkit for future effects without triggering -Wunused-function.
    [[maybe_unused]] Sound makeNoiseBurst(float durationSec, float volume, float lowpassCutoff)
    {
        const int frameCount = static_cast<int>(
            durationSec * static_cast<float>(AUDIO_SAMPLE_RATE));
        std::int16_t* data = allocSampleBuffer(frameCount);
        if (data == nullptr) { return Sound{}; }

        const float blend = std::clamp(lowpassCutoff, 0.0f, 1.0f);
        float prev = 0.0f;
        for (int i = 0; i < frameCount; ++i) {
            // Centre rand() around 0 so the noise is symmetric.
            const float rnd =
                (static_cast<float>(std::rand()) /
                 static_cast<float>(RAND_MAX)) * 2.0f - 1.0f;
            // One-pole lowpass: prev = prev + blend * (rnd - prev).
            prev += blend * (rnd - prev);
            const float envelope = adsrEnvelope(i, frameCount);
            const float sample   = prev * envelope * volume;
            data[i] = toInt16(sample);
        }
        return waveToSound(data, frameCount);
    }

    /// Build a Sound that is the sum of `count` sine waves at the listed
    /// frequencies, each with the same envelope and overall `volume`. Used for
    /// the wave-clear chord, the game-over chord, and the shield shimmer.
    Sound makeChord(const float* freqs, int count,
                    float durationSec, float volume)
    {
        const int frameCount = static_cast<int>(
            durationSec * static_cast<float>(AUDIO_SAMPLE_RATE));
        std::int16_t* data = allocSampleBuffer(frameCount);
        if (data == nullptr || count <= 0) {
            return waveToSound(data, frameCount);
        }

        // Per-voice scale so summing `count` sines never overflows. Adding a
        // small headroom factor (0.9) avoids clipping on the louder notes.
        const float perVoice = volume * 0.9f / static_cast<float>(count);
        for (int i = 0; i < frameCount; ++i) {
            const float t = static_cast<float>(i) /
                            static_cast<float>(AUDIO_SAMPLE_RATE);
            float mix = 0.0f;
            for (int v = 0; v < count; ++v) {
                mix += std::sin(AUDIO_TWO_PI * freqs[v] * t);
            }
            const float envelope = adsrEnvelope(i, frameCount);
            data[i] = toInt16(mix * perVoice * envelope);
        }
        return waveToSound(data, frameCount);
    }

    /// Build a Sound by mixing a noise burst (with a soft lowpass) and a
    /// frequency-swept tone, each scaled by its own level. Used for sounds
    /// that need both percussive noise and a pitched component (fire, melee,
    /// nova, dash, blink). The two components share one envelope so the
    /// combined sound starts and ends smoothly.
    Sound makeNoiseAndTone(float durationSec,
                           float freqStart, float freqEnd,
                           float noiseLevel, float toneLevel,
                           float volume,
                           float lowpassCutoff = NOISE_LOWPASS_DEFAULT)
    {
        const int frameCount = static_cast<int>(
            durationSec * static_cast<float>(AUDIO_SAMPLE_RATE));
        std::int16_t* data = allocSampleBuffer(frameCount);
        if (data == nullptr) { return Sound{}; }

        const float blend = std::clamp(lowpassCutoff, 0.0f, 1.0f);
        float prevNoise = 0.0f;
        float phase     = 0.0f;
        for (int i = 0; i < frameCount; ++i) {
            // Noise component (lowpassed).
            const float rnd =
                (static_cast<float>(std::rand()) /
                 static_cast<float>(RAND_MAX)) * 2.0f - 1.0f;
            prevNoise += blend * (rnd - prevNoise);

            // Tone component (frequency sweep).
            const float t = static_cast<float>(i) /
                            static_cast<float>(frameCount > 1 ? frameCount - 1 : 1);
            const float freq = freqStart + (freqEnd - freqStart) * t;
            phase += AUDIO_TWO_PI * freq /
                     static_cast<float>(AUDIO_SAMPLE_RATE);
            const float tone = std::sin(phase);

            const float envelope = adsrEnvelope(i, frameCount);
            const float mixed    = (prevNoise * noiseLevel +
                                    tone * toneLevel) * envelope * volume;
            data[i] = toInt16(mixed);
        }
        return waveToSound(data, frameCount);
    }

    /// Build a Sound that contains TWO ascending sine notes back-to-back
    /// inside a single buffer (used for the pickup chime). Each note occupies
    /// half the buffer; both share the same envelope shape so the chime feels
    /// musical rather than choppy.
    Sound makeTwoNoteChime(float durationSec, float freqLow, float freqHigh,
                           float volume)
    {
        const int frameCount = static_cast<int>(
            durationSec * static_cast<float>(AUDIO_SAMPLE_RATE));
        const int halfFrame  = frameCount / 2;
        std::int16_t* data = allocSampleBuffer(frameCount);
        if (data == nullptr) { return Sound{}; }

        float phase = 0.0f;
        for (int i = 0; i < frameCount; ++i) {
            const bool firstHalf = (i < halfFrame);
            const float freq = firstHalf ? freqLow : freqHigh;
            phase += AUDIO_TWO_PI * freq /
                     static_cast<float>(AUDIO_SAMPLE_RATE);
            // Per-note envelope: each note has its own attack/release inside
            // its half of the buffer so neither half clicks at its boundary.
            const int   localIndex = firstHalf ? i : (i - halfFrame);
            const int   localTotal = (firstHalf ? halfFrame
                                                : (frameCount - halfFrame));
            const float envelope   = adsrEnvelope(localIndex, localTotal);
            data[i] = toInt16(std::sin(phase) * envelope * volume);
        }
        return waveToSound(data, frameCount);
    }

    /// Build a "villain laugh" Sound — a procedural cartoon-villain cackle
    /// built from a series of LOW pulsations whose pitches descend over time.
    /// Each pulsation is a short chunk of mid-range tone (think "hah") with
    /// a sharp attack and short decay; stringing five or six of them in a
    /// row at decreasing pitches gives an unmistakable "muahahaha" cadence.
    /// A subtle low-frequency growl is mixed in throughout so the laugh feels
    /// menacing instead of comedic.
    /// @param durationSec total length of the laugh in seconds.
    /// @param volume      master gain in [0, 1].
    /// @return a Sound owning its own audio-driver buffer.
    Sound makeVillainLaugh(float durationSec, float volume)
    {
        const int frameCount = static_cast<int>(
            durationSec * static_cast<float>(AUDIO_SAMPLE_RATE));
        std::int16_t* data = allocSampleBuffer(frameCount);
        if (data == nullptr) { return Sound{}; }

        // Six pulsations whose pitch descends from "ha!" down to a deep
        // chest-laugh growl. Tuned so the swing between each "hah" sounds
        // like an exhale rather than a chord change.
        constexpr int   kPulsationCount   = 6;
        constexpr float kPulsationFreqs[kPulsationCount] = {
            260.0f, 220.0f, 196.0f, 175.0f, 155.0f, 140.0f
        };
        // Underlying low growl carries through the whole laugh.
        constexpr float kGrowlFreqHz     = 70.0f;
        constexpr float kGrowlMixLevel   = 0.30f;
        // Per-pulsation tone gain (before master volume).
        constexpr float kPulsationLevel  = 0.85f;

        const int framesPerPulsation = frameCount / kPulsationCount;

        float phaseTone  = 0.0f;
        float phaseGrowl = 0.0f;

        for (int i = 0; i < frameCount; ++i) {
            // Which pulsation are we currently inside?
            const int pulseIndex = std::min(i / framesPerPulsation,
                                            kPulsationCount - 1);
            const int localIndex = i - pulseIndex * framesPerPulsation;
            const int localTotal = framesPerPulsation;

            // Pulse envelope: very fast attack, hard exponential decay so
            // each "hah" is a sharp burst followed by a brief gap.
            const float t = static_cast<float>(localIndex) /
                            static_cast<float>(localTotal > 1 ? localTotal - 1 : 1);
            // Pulse decays as exp(-5*t); attack is the first 8% rising linearly.
            const float attack = std::min(1.0f, t * 12.0f);
            const float decay  = std::exp(-5.0f * t);
            const float pulseEnv = attack * decay;

            // Each pulsation rides at its own descending frequency.
            const float pulseFreq = kPulsationFreqs[pulseIndex];
            phaseTone += AUDIO_TWO_PI * pulseFreq /
                         static_cast<float>(AUDIO_SAMPLE_RATE);
            // Steady low growl underneath every pulsation.
            phaseGrowl += AUDIO_TWO_PI * kGrowlFreqHz /
                          static_cast<float>(AUDIO_SAMPLE_RATE);

            const float pulseSample = std::sin(phaseTone) * pulseEnv *
                                      kPulsationLevel;
            // Growl uses a slow overall fade-in so it builds up across the
            // laugh; envelope from the whole-buffer index (i / frameCount).
            const float growlFade   = static_cast<float>(i) /
                                      static_cast<float>(frameCount > 1 ? frameCount - 1 : 1);
            const float growlSample = std::sin(phaseGrowl) * kGrowlMixLevel *
                                      (0.4f + 0.6f * growlFade);

            const float mix = (pulseSample + growlSample) * volume;
            data[i] = toInt16(std::clamp(mix, -1.0f, 1.0f));
        }
        return waveToSound(data, frameCount);
    }
    //
    // raylib calls audioStreamCallback on its audio thread whenever the music
    // stream needs more samples. The callback writes interleaved stereo int16
    // PCM frames computed from two slow detuned sines plus a slow LFO that
    // breathes the overall amplitude. State (the running phases / sample
    // counter) is held in a static struct because the callback signature has
    // no user-data pointer.

    /// Per-stream synth state for the ambient music callback.
    /// One running phase per chord voice plus the LFO phase. Defaults are
    /// zero so the chord begins phase-aligned at the start of the stream.
    struct MusicState {
        float phases[MUSIC_PAD_VOICE_COUNT] = {0.0f, 0.0f, 0.0f, 0.0f};
        float phaseLfo = 0.0f;
    };

    /// Single instance of the music synth state. We keep this static to the
    /// translation unit because raylib's AudioCallback signature has no
    /// user-data slot. Only one music stream is ever active at a time, so
    /// sharing a single state across callbacks is correct.
    MusicState g_musicState;

    /// raylib audio callback — fills `bufferData` with `frames` stereo frames
    /// of int16 PCM data synthesised from g_musicState. Signature is fixed by
    /// raylib's AudioCallback typedef.
    /// @param bufferData void* pointer the raylib audio thread expects to be
    ///                   written with `frames` interleaved stereo int16 frames.
    /// @param frames     number of stereo frames the audio thread is asking for.
    void audioStreamCallback(void* bufferData, unsigned int frames)
    {
        if (bufferData == nullptr || frames == 0) { return; }
        auto* out = static_cast<std::int16_t*>(bufferData);

        // Pre-compute the per-sample phase increments so the inner loop is a
        // pure sin() pipeline. One increment per chord voice plus the LFO.
        float incPhases[MUSIC_PAD_VOICE_COUNT];
        for (int v = 0; v < MUSIC_PAD_VOICE_COUNT; ++v) {
            incPhases[v] =
                AUDIO_TWO_PI * MUSIC_PAD_FREQS_HZ[v] /
                static_cast<float>(AUDIO_SAMPLE_RATE);
        }
        const float incLfo =
            AUDIO_TWO_PI * MUSIC_LFO_RATE_HZ /
            static_cast<float>(AUDIO_SAMPLE_RATE);

        for (unsigned int f = 0; f < frames; ++f) {
            // Slow amplitude breath: lfo in [-1,1] → in (1-depth, 1).
            const float lfo = std::sin(g_musicState.phaseLfo);
            const float amp = 1.0f -
                              MUSIC_LFO_DEPTH * 0.5f * (1.0f - lfo);

            // Sum every chord voice with equal per-voice gain. Dividing by the
            // voice count keeps the peak amplitude bounded regardless of how
            // many voices the chord has.
            float mix = 0.0f;
            for (int v = 0; v < MUSIC_PAD_VOICE_COUNT; ++v) {
                mix += std::sin(g_musicState.phases[v]);
            }
            mix *= MUSIC_VOICE_LEVEL * amp /
                   static_cast<float>(MUSIC_PAD_VOICE_COUNT);

            // Convert once and write to both stereo channels.
            const std::int16_t pcm = toInt16(mix);
            out[f * 2 + 0] = pcm; // Left.
            out[f * 2 + 1] = pcm; // Right.

            // Advance phases (wrap when they grow large to keep precision).
            for (int v = 0; v < MUSIC_PAD_VOICE_COUNT; ++v) {
                g_musicState.phases[v] += incPhases[v];
                if (g_musicState.phases[v] > AUDIO_TWO_PI) {
                    g_musicState.phases[v] -= AUDIO_TWO_PI;
                }
            }
            g_musicState.phaseLfo += incLfo;
            if (g_musicState.phaseLfo > AUDIO_TWO_PI) {
                g_musicState.phaseLfo -= AUDIO_TWO_PI;
            }
        }
    }


} // anonymous namespace

// =============================================================================
// Construction / destruction
// =============================================================================

/// Open the raylib window, set the target frame rate, and initialise every
/// cache field to "nothing yet" so the first draw call composes from scratch.
RaylibRenderer::RaylibRenderer()
    : pendingReset_(false)
    , gameFrameActive_(false)
    , cachedState_(nullptr)
    , cachedConfig_(nullptr)
    , cachedLog_(nullptr)
    , menuActive_(false)
    , menuOptions_()
    , menuSelected_(0)
    , messageLines_()
    , firePromptActive_(false)
    , dashPromptActive_(false)
    , fireTrailCells_()
    , fireImpactCell_(0, 0)
    , fireTrailFramesRemaining_(0)
    , fireTrailHit_(false)
    , enemyAttacks_()
    , novaCenter_(0, 0)
    , novaRadius_(0)
    , novaFramesRemaining_(0)
    , meleeEffectCell_(0, 0)
    , meleeEffectFramesRemaining_(0)
    , transientNotice_()
    , transientNoticeFrames_(0)
    , hellModeLastWaveNumber_(-1)
    , hellModeWaveStartTime_(0.0)
    , hellModeActive_(false)
    , hellModeLaughTriggered_(false)
    , hellEmbers_()
    , hellEmbersInitialised_(false)
    , audioReady_(false)
    , fireSound_{}
    , hitSound_{}
    , meleeSound_{}
    , enemyHitSound_{}
    , novaSound_{}
    , waveClearSound_{}
    , gameOverSound_{}
    , pickupSound_{}
    , dashSound_{}
    , shieldSound_{}
    , blinkSound_{}
    , villainLaughSound_{}
    , musicStream_{}
    , bossMusic_{}
    , bossMusicLoaded_(false)
    , bossMusicPlaying_(false)
    , normalMusic_{}
    , normalMusicLoaded_(false)
    , normalMusicPlaying_(false)
    , masterMusicVolume_(1.0f)
{
    // Open the OS window and the OpenGL context. Title is the const char*
    // shown on the title bar; size is the window's INITIAL client area.
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE);

    // Cap the frame rate so the wait loop in pollInput does not spin at the
    // monitor's full refresh rate; 60 fps is responsive without burning CPU.
    SetTargetFPS(TARGET_FPS);

    // ---- Audio: open the audio device and synthesise every effect --------
    //
    // InitAudioDevice can fail (no audio hardware, driver issue) — in that
    // case we leave audioReady_ false and every later PlaySound / Unload call
    // is gated on the flag. The game still renders perfectly, just silently.
    InitAudioDevice();
    audioReady_ = IsAudioDeviceReady();

    if (audioReady_) {
        // Build every SFX in memory from sine / square / noise samples. The
        // Sound returned by each helper owns its own audio-driver buffer; the
        // destructor unloads them via UnloadSound.
        fireSound_      = makeNoiseAndTone(FIRE_SOUND_DURATION_SEC,
                                           FIRE_SOUND_FREQ_START_HZ,
                                           FIRE_SOUND_FREQ_END_HZ,
                                           FIRE_SOUND_NOISE_LEVEL,
                                           FIRE_SOUND_TONE_LEVEL,
                                           AUDIO_SFX_VOLUME);
        hitSound_       = makeBeepSound(HIT_SOUND_FREQ_HZ,
                                        HIT_SOUND_FREQ_HZ,
                                        HIT_SOUND_DURATION_SEC,
                                        HIT_SOUND_LEVEL * AUDIO_SFX_VOLUME);
        meleeSound_     = makeNoiseAndTone(MELEE_SOUND_DURATION_SEC,
                                           MELEE_SOUND_FREQ_HZ,
                                           MELEE_SOUND_FREQ_HZ * 0.6f,
                                           MELEE_SOUND_NOISE_LEVEL,
                                           MELEE_SOUND_TONE_LEVEL,
                                           AUDIO_SFX_VOLUME);
        enemyHitSound_  = makeBeepSound(ENEMY_HIT_SOUND_FREQ_HZ,
                                        ENEMY_HIT_SOUND_FREQ_HZ * 0.6f,
                                        ENEMY_HIT_SOUND_DURATION_SEC,
                                        ENEMY_HIT_SOUND_LEVEL * AUDIO_SFX_VOLUME);
        novaSound_      = makeNoiseAndTone(NOVA_SOUND_DURATION_SEC,
                                           NOVA_SOUND_FREQ_START_HZ,
                                           NOVA_SOUND_FREQ_END_HZ,
                                           NOVA_SOUND_NOISE_LEVEL,
                                           NOVA_SOUND_TONE_LEVEL,
                                           AUDIO_SFX_VOLUME);
        waveClearSound_ = makeChord(WAVE_CLEAR_FREQS_HZ, WAVE_CLEAR_FREQ_COUNT,
                                    WAVE_CLEAR_DURATION_SEC,
                                    WAVE_CLEAR_LEVEL * AUDIO_SFX_VOLUME);
        gameOverSound_  = makeChord(GAME_OVER_FREQS_HZ, GAME_OVER_FREQ_COUNT,
                                    GAME_OVER_DURATION_SEC,
                                    GAME_OVER_LEVEL * AUDIO_SFX_VOLUME);
        pickupSound_    = makeTwoNoteChime(PICKUP_DURATION_SEC,
                                           PICKUP_FREQ_LOW_HZ,
                                           PICKUP_FREQ_HIGH_HZ,
                                           PICKUP_LEVEL * AUDIO_SFX_VOLUME);
        dashSound_      = makeNoiseAndTone(DASH_SOUND_DURATION_SEC,
                                           DASH_SOUND_FREQ_START_HZ,
                                           DASH_SOUND_FREQ_END_HZ,
                                           DASH_SOUND_NOISE_LEVEL,
                                           DASH_SOUND_TONE_LEVEL,
                                           AUDIO_SFX_VOLUME);
        shieldSound_    = makeChord(SHIELD_FREQS_HZ, SHIELD_FREQ_COUNT,
                                    SHIELD_SOUND_DURATION_SEC,
                                    SHIELD_SOUND_LEVEL * AUDIO_SFX_VOLUME);
        blinkSound_     = makeNoiseAndTone(BLINK_SOUND_DURATION_SEC,
                                           BLINK_SOUND_FREQ_START_HZ,
                                           BLINK_SOUND_FREQ_END_HZ,
                                           BLINK_SOUND_NOISE_LEVEL,
                                           BLINK_SOUND_TONE_LEVEL,
                                           AUDIO_SFX_VOLUME);

        // Villain laugh — built procedurally from descending pulsations + a
        // low growl so the player hears a deep "muahahaha" cackle whenever
        // they die or whenever a Death Dungeon mode shift kicks in. ~1.6 s
        // long so the cackle has room for six pulsations without rushing.
        villainLaughSound_ = makeVillainLaugh(1.6f, 0.95f * AUDIO_SFX_VOLUME);

        // ---- Ambient music drone -------------------------------------
        //
        // LoadAudioStream prepares an empty PCM stream the audio thread can
        // pull from; SetAudioStreamCallback hands raylib our synth callback
        // (audioStreamCallback) which fills each requested buffer on the fly.
        // Volume is set low so the drone sits underneath the SFX, then the
        // stream is started so it plays for as long as the renderer lives.
        musicStream_ = LoadAudioStream(
            static_cast<unsigned int>(AUDIO_SAMPLE_RATE),
            static_cast<unsigned int>(AUDIO_SAMPLE_SIZE),
            static_cast<unsigned int>(AUDIO_MUSIC_CHANNELS));
        SetAudioStreamCallback(musicStream_, audioStreamCallback);
        SetAudioStreamVolume(musicStream_, AUDIO_MUSIC_VOLUME * masterMusicVolume_);
        PlayAudioStream(musicStream_);

        // ---- Boss-wave background track ------------------------------
        //
        // Streamed from disk so the OGG file is decoded on raylib's audio
        // thread, not loaded fully into RAM. If the file is missing or the
        // decoder rejects the format, raylib returns a Music whose internal
        // ctxData is null; we detect that and just leave bossMusicLoaded_
        // false so every later setBossMusicActive call is a silent no-op.
        bossMusic_ = LoadMusicStream(BOSS_MUSIC_PATH);
        bossMusicLoaded_ = (bossMusic_.ctxData != nullptr);
        if (bossMusicLoaded_) {
            bossMusic_.looping = true;
            SetMusicVolume(bossMusic_, BOSS_MUSIC_VOLUME * masterMusicVolume_);
        }

        // ---- Normal-wave background track ----------------------------
        //
        // Same loading discipline as the boss track: streamed from disk so
        // the decoder runs on raylib's audio thread, looped automatically,
        // gated on a *_Loaded_ flag so a missing file silently disables the
        // feature instead of crashing.
        normalMusic_ = LoadMusicStream(NORMAL_MUSIC_PATH);
        normalMusicLoaded_ = (normalMusic_.ctxData != nullptr);
        if (normalMusicLoaded_) {
            normalMusic_.looping = true;
            SetMusicVolume(normalMusic_, BOSS_MUSIC_VOLUME * masterMusicVolume_);
        }
    }
}

/// Tear down the raylib graphics device. CloseWindow releases the OpenGL
/// context and the OS-level window resources. Audio resources (every Sound,
/// the music stream, and the audio device itself) are released first because
/// their cleanup depends on the audio device still being valid.
RaylibRenderer::~RaylibRenderer()
{
    if (audioReady_) {
        // Stop and unload the streamed tracks first so their decoder threads
        // are shut down before the audio device closes.
        if (bossMusicLoaded_) {
            StopMusicStream(bossMusic_);
            UnloadMusicStream(bossMusic_);
            bossMusicLoaded_  = false;
            bossMusicPlaying_ = false;
        }
        if (normalMusicLoaded_) {
            StopMusicStream(normalMusic_);
            UnloadMusicStream(normalMusic_);
            normalMusicLoaded_  = false;
            normalMusicPlaying_ = false;
        }

        // Stop and unload the music stream BEFORE the device is closed so
        // raylib can flush any in-flight buffers cleanly.
        StopAudioStream(musicStream_);
        UnloadAudioStream(musicStream_);

        // Free every SFX. UnloadSound is safe on a default-initialised Sound
        // (raylib treats a null buffer as a no-op), but we have populated all
        // of them in the constructor when audioReady_ is true.
        UnloadSound(fireSound_);
        UnloadSound(hitSound_);
        UnloadSound(meleeSound_);
        UnloadSound(enemyHitSound_);
        UnloadSound(novaSound_);
        UnloadSound(waveClearSound_);
        UnloadSound(gameOverSound_);
        UnloadSound(pickupSound_);
        UnloadSound(dashSound_);
        UnloadSound(shieldSound_);
        UnloadSound(blinkSound_);
        UnloadSound(villainLaughSound_);

        CloseAudioDevice();
    }
    CloseWindow();
}

// =============================================================================
// resetCompositionIfNeeded - drop cached menu/message overlays after a poll
// =============================================================================

/// If pollInput recently returned, the next public draw call begins a brand
/// new composition: clear the menu and message caches and the fire prompt
/// flag, then drop pendingReset_ so subsequent draw calls in the same frame
/// (e.g. drawMessage + drawMenu + drawMessage) continue stacking.
void RaylibRenderer::resetCompositionIfNeeded()
{
    if (!pendingReset_) { return; }

    menuActive_       = false;
    menuOptions_.clear();
    menuSelected_     = 0;
    messageLines_.clear();
    firePromptActive_ = false;
    dashPromptActive_ = false;
    // gameFrameActive_ and cachedState_/Config_/Log_ are intentionally left
    // alone here: a Save -> drawMessage flow in Game.cpp wants the message to
    // appear ON TOP of the current game frame, and drawFrame replaces these
    // pointers explicitly when a fresh world is to be drawn.

    pendingReset_ = false;
}

// =============================================================================
// renderCurrentScreen - one BeginDrawing/EndDrawing pass over the full cache
// =============================================================================

/// Paint a single complete frame from the cached composition state. This is
/// the only function in the file that calls BeginDrawing / EndDrawing, so the
/// caller never has to think about raylib's drawing-mode discipline.
///
/// Drawing order is back-to-front:
///   1. ClearBackground - wipes the previous frame to solid black.
///   2. Game frame      - map cells, items, enemies, player, HUD, controls.
///   3. Menu overlay    - centred panel of options (if menuActive_).
///   4. Message overlay - stacked text near the top (if any messages cached).
///   5. Fire prompt     - small centred prompt shown only while waiting on
///      the second key of a Fire command.
void RaylibRenderer::renderCurrentScreen()
{
    // Keep the streamed music decoders fed once per frame. raylib's
    // UpdateMusicStream is a no-op if the stream is not currently playing, so
    // calling it unconditionally while the track is loaded is safe and means
    // we do not have to remember the play/stop state at every callsite.
    if (audioReady_ && bossMusicLoaded_ && bossMusicPlaying_) {
        UpdateMusicStream(bossMusic_);
    }
    if (audioReady_ && normalMusicLoaded_ && normalMusicPlaying_) {
        UpdateMusicStream(normalMusic_);
    }

    BeginDrawing();
    ClearBackground(BLACK);

    if (gameFrameActive_) {
        renderGameFrame();
    }
    if (menuActive_) {
        renderMenuOverlay();
    }
    if (!messageLines_.empty()) {
        renderMessageOverlay();
    }
    if (firePromptActive_) {
        renderFirePreview();
        renderFirePromptOverlay();
    }
    if (dashPromptActive_) {
        renderDashPromptOverlay();
    }

    EndDrawing();
}

// =============================================================================
// renderGameFrame - draw the map, entities, items, HUD, and controls strip
// =============================================================================

/// Render the cached game world. Walks the map row by row, drawing the tile
/// rectangle first and then any item / enemy / player sitting on that tile on
/// top of it. Finally draws the HUD panel and the controls strip.
///
/// This function ASSUMES BeginDrawing has been called by renderCurrentScreen
/// and that all three cached pointers (state, config, log) are non-null. The
/// gameFrameActive_ flag in the caller guards both invariants.
void RaylibRenderer::renderGameFrame() const
{
    if (cachedState_ == nullptr || cachedConfig_ == nullptr ||
        cachedLog_   == nullptr) {
        // Defensive: the gameFrameActive_ guard should make this unreachable,
        // but a null check costs nothing and prevents a crash if it ever is.
        return;
    }

    const GameState& state = *cachedState_;

    // ---- Wave-change tracking (drives hell-mode tile palette swap) -------
    //
    // Hell mode is fully managed by renderHellMode (called later in the same
    // frame), but the tile loop below also needs to know whether we are in
    // hell mode RIGHT NOW so it can pick the bloody-brick palette. We update
    // the wave tracker and the hellModeActive_ flag here at the very top of
    // the frame so every downstream draw call sees a consistent view.
    {
        const double now      = GetTime();
        const int    waveNow  = state.waveNumber();
        if (waveNow != hellModeLastWaveNumber_) {
            hellModeLastWaveNumber_ = waveNow;
            hellModeWaveStartTime_  = now;
            hellModeActive_         = false;
            hellModeLaughTriggered_ = false;
            hellEmbers_.clear();
            hellEmbersInitialised_  = false;
        }
        // Boss waves get a longer build-up than normal waves: the boss fight
        // soundtrack has more drama on its own, so the visual escalation kicks
        // in later (~19 s) instead of the standard ~9.5 s for normal waves.
        const bool   isBossWave =
            waveNow > 0 &&
            (waveNow % HELL_MODE_BOSS_WAVE_MODULUS) == 0;
        const double triggerSeconds =
            isBossWave ? HELL_MODE_TRIGGER_SECONDS_BOSS
                       : HELL_MODE_TRIGGER_SECONDS_NORMAL;
        const double elapsed = now - hellModeWaveStartTime_;
        if (!hellModeActive_ && elapsed >= triggerSeconds) {
            hellModeActive_ = true;
        }
        // Fire the villain laugh sting EXACTLY at the trigger boundary,
        // separately from hellModeActive_ so we do not race with frame timing
        // (the laugh latches once per wave). The laugh punctuates the mode
        // shift and matches the visual escalation.
        if (hellModeActive_ && !hellModeLaughTriggered_) {
            if (audioReady_) {
                PlaySound(villainLaughSound_);
            }
            hellModeLaughTriggered_ = true;
        }
    }
    const bool hellTilesActive = hellModeActive_;

    // ---- Map cells (walls and floors) ------------------------------------
    //
    // Iterate the visible window of MAP_COLUMNS x MAP_ROWS cells. Cells that
    // fall outside the actual map's bounds are drawn as Wall (matches
    // GridMap's "outside == wall" semantics) so the play area always has a
    // solid background and never reveals raw black showing through gaps.
    const GridMap& map = state.map();
    const int mapW = map.width();
    const int mapH = map.height();

    for (int y = 0; y < MAP_ROWS; ++y) {
        for (int x = 0; x < MAP_COLUMNS; ++x) {
            const int pixelX = x * CELL_SIZE;
            const int pixelY = y * CELL_SIZE;

            // Determine whether this cell is a wall or a floor (cells outside
            // the actual map are drawn as walls, matching GridMap's
            // "outside == wall" semantics so the playfield never reveals raw
            // black behind the world).
            const bool isFloor =
                (x < mapW && y < mapH) &&
                (map.typeAt(Vec2(x, y)) == TileType::Floor);

            if (isFloor) {
                if (hellTilesActive) {
                    // ---- Hell-mode floor: dark stone with a subtle blood-
                    // stain pattern. A simple deterministic checker (based on
                    // cell coordinates) keeps neighbouring cells from looking
                    // like a perfectly flat colour without needing a texture
                    // asset.
                    DrawRectangle(pixelX, pixelY, CELL_SIZE, CELL_SIZE,
                                  COLOR_FLOOR_HELL);
                    const bool stained = ((x * 7 + y * 13) % 11) < 3;
                    if (stained) {
                        DrawRectangle(pixelX + 2, pixelY + 2,
                                      CELL_SIZE - 4, CELL_SIZE - 4,
                                      COLOR_FLOOR_HELL_STAIN);
                    }
                } else {
                    // ---- Calm floor: flat dark gray, no embellishment.
                    DrawRectangle(pixelX, pixelY, CELL_SIZE, CELL_SIZE,
                                  COLOR_FLOOR);
                }
            } else {
                if (hellTilesActive) {
                    // ---- Hell-mode wall: bloody brick — base fill, lighter
                    // top edge (highlight) and darker bottom edge (shadow) so
                    // the wall tiles read as masonry rather than a flat block.
                    DrawRectangle(pixelX, pixelY, CELL_SIZE, CELL_SIZE,
                                  COLOR_WALL_HELL);
                    DrawRectangle(pixelX, pixelY,
                                  CELL_SIZE, 3,
                                  COLOR_WALL_HELL_HIGHLIGHT);
                    DrawRectangle(pixelX, pixelY + CELL_SIZE - 3,
                                  CELL_SIZE, 3,
                                  COLOR_WALL_HELL_SHADOW);
                } else {
                    // ---- Calm wall: flat neutral gray, no accents.
                    DrawRectangle(pixelX, pixelY, CELL_SIZE, CELL_SIZE,
                                  COLOR_WALL);
                }
            }
        }
    }

    // ---- Items on the floor ----------------------------------------------
    //
    // Drawn before enemies / player so that an item stacked under an enemy is
    // visually overdrawn by the threat (enemies and the player take priority).
    for (const auto& itemPtr : state.items()) {
        if (!itemPtr) { continue; }
        const Vec2 pos = itemPtr->position();
        if (pos.x < 0 || pos.x >= MAP_COLUMNS ||
            pos.y < 0 || pos.y >= MAP_ROWS) {
            continue; // Off-screen item; do not draw outside the map area.
        }

        // Draw a single character with the item's signature colour at the
        // centre of its cell.
        const char glyph[2] = { itemPtr->glyph(), '\0' };
        const Color color   = itemColorFor(itemPtr->kind());
        DrawText(glyph,
                 pos.x * CELL_SIZE + GLYPH_INSET,
                 pos.y * CELL_SIZE + GLYPH_INSET,
                 GLYPH_FONT_SIZE,
                 color);
    }

    // ---- Enemies ---------------------------------------------------------
    for (const auto& enemyPtr : state.enemies()) {
        if (!enemyPtr) { continue; }
        const Vec2 pos = enemyPtr->position();
        if (pos.x < 0 || pos.x >= MAP_COLUMNS ||
            pos.y < 0 || pos.y >= MAP_ROWS) {
            continue;
        }

        const char glyph[2] = { enemyPtr->glyph(), '\0' };
        const Color color   = entityColorFor(enemyPtr->kind());
        DrawText(glyph,
                 pos.x * CELL_SIZE + GLYPH_INSET,
                 pos.y * CELL_SIZE + GLYPH_INSET,
                 GLYPH_FONT_SIZE,
                 color);

        // ---- Enemy HP bar ------------------------------------------------
        //
        // Draw a small bar JUST above the enemy's cell so the player can
        // see at a glance which enemies are wounded. The bar is shown for
        // every enemy, including those at full HP, so that a freshly-spawned
        // healthy threat is also unmistakably labelled. The fill colour
        // follows the same green / yellow / red ramp as the player HP bar
        // for visual consistency.
        const int hp    = enemyPtr->health();
        const int hpMax = (enemyPtr->maxHealth() > 0) ? enemyPtr->maxHealth() : 1;
        const float ratio = std::clamp(static_cast<float>(hp) /
                                       static_cast<float>(hpMax),
                                       0.0f, 1.0f);
        const int filled = static_cast<int>(ENEMY_HP_BAR_WIDTH * ratio);
        const Color fillColor = hpBarColorFor(ratio);

        // Horizontally centre the bar inside the cell so it lines up with
        // the glyph regardless of CELL_SIZE.
        const int barX = pos.x * CELL_SIZE + (CELL_SIZE - ENEMY_HP_BAR_WIDTH) / 2;
        const int barY = pos.y * CELL_SIZE + ENEMY_HP_BAR_Y_OFFSET;

        // Background first (dark gray) then the coloured fill on top so a
        // partial HP bar shows the missing portion as a clear empty band.
        DrawRectangle(barX, barY,
                      ENEMY_HP_BAR_WIDTH, ENEMY_HP_BAR_HEIGHT,
                      COLOR_BAR_BG);
        DrawRectangle(barX, barY,
                      filled, ENEMY_HP_BAR_HEIGHT,
                      fillColor);
    }

    // ---- Player (highest priority) ---------------------------------------
    //
    // The player must be unmistakable on screen even when surrounded by a
    // cluster of enemy glyphs. We layer three accents under/around the @:
    //   1. A soft translucent glow ring at ~2-cell radius (faint hint).
    //   2. A bright cyan/blue solid disc filling the cell beneath the glyph.
    //   3. A static thicker border around the cell itself.
    // Then the @ glyph is drawn on top in the player colour.
    {
        const Vec2 pos = state.player().position();
        if (pos.x >= 0 && pos.x < MAP_COLUMNS &&
            pos.y >= 0 && pos.y < MAP_ROWS) {
            const int centerX = pos.x * CELL_SIZE + CELL_SIZE / 2;
            const int centerY = pos.y * CELL_SIZE + CELL_SIZE / 2;

            // Glow halo first so it sits behind everything else (low alpha so
            // floor / items two cells away remain readable).
            DrawCircle(centerX, centerY, static_cast<float>(PLAYER_GLOW_RADIUS),
                       COLOR_PLAYER_GLOW);

            // Solid disc immediately behind the glyph for the strongest
            // "this is the player" cue.
            DrawCircle(centerX, centerY,
                       static_cast<float>(PLAYER_HIGHLIGHT_RADIUS),
                       COLOR_PLAYER_DISC);

            // Static thicker border around the player's cell. (A real pulse
            // would tween the alpha each frame; the static border is enough
            // to make the cell pop without complicating the render loop.)
            for (int t = 0; t < PLAYER_BORDER_THICKNESS; ++t) {
                DrawRectangleLines(pos.x * CELL_SIZE + t,
                                   pos.y * CELL_SIZE + t,
                                   CELL_SIZE - 2 * t,
                                   CELL_SIZE - 2 * t,
                                   COLOR_PLAYER_BORDER);
            }

            // Shield visual indicator: when the player has an active shield,
            // draw a bright white ring slightly larger than the normal disc so
            // the damage-immunity state is instantly visible on the map. The
            // ring radius matches CELL_SIZE (slightly beyond the normal glow)
            // and pulses by toggling between white and cyan every ~0.5 s using
            // the frame time, giving a subtle "energy field" shimmer.
            if (cachedState_ != nullptr &&
                cachedState_->player().isShielded()) {
                // Pulsing between white and cyan based on elapsed time.
                const float shieldPulse =
                    static_cast<float>(std::sin(GetTime() * 6.0)) * 0.5f + 0.5f;
                const unsigned char r = static_cast<unsigned char>(
                    200 + static_cast<int>(55.0f * shieldPulse));
                const unsigned char g = static_cast<unsigned char>(
                    240 + static_cast<int>(15.0f * shieldPulse));
                const Color shieldColor = { r, g, 255, 230 };
                DrawCircleLines(centerX, centerY,
                                static_cast<float>(CELL_SIZE),
                                shieldColor);
                // Draw a second slightly smaller ring for thickness / emphasis.
                DrawCircleLines(centerX, centerY,
                                static_cast<float>(CELL_SIZE) - 1.0f,
                                shieldColor);
            }

            // Finally the @ glyph itself.
            const char glyph[2] = { state.player().glyph(), '\0' };
            DrawText(glyph,
                     pos.x * CELL_SIZE + GLYPH_INSET,
                     pos.y * CELL_SIZE + GLYPH_INSET,
                     GLYPH_FONT_SIZE,
                     COLOR_PLAYER);
        }
    }

    // ---- Transient fire effect (R16 visual cue) --------------------------
    //
    // While the countdown is positive we draw a tracer line through every
    // cell the projectile passed (skipping the player's own cell) and a
    // brighter flash on the impact cell. The trail and impact colours both
    // differ between hit and miss so the player can read the outcome from
    // either part of the effect at a glance. The countdown ticks here so the
    // effect ages out with each rendered frame.
    if (fireTrailFramesRemaining_ > 0) {
        // Pick the trail colour up front from the cached hit/miss flag so the
        // whole tracer is uniformly red (hit) or smoky gray (miss).
        const Color trailColor = fireTrailHit_ ? COLOR_FIRE_TRAIL_HIT
                                               : COLOR_FIRE_TRAIL_MISS;

        // Tracer line: connect every consecutive cell pair in the trail with
        // a thick segment so the projectile path reads clearly. The first
        // cell is the player's own tile, so we start the line draws at index
        // 1 to avoid stamping the trail through the player highlight.
        for (std::size_t i = 1; i < fireTrailCells_.size(); ++i) {
            const Vec2& a = fireTrailCells_[i - 1];
            const Vec2& b = fireTrailCells_[i];
            // Cull cells outside the visible map area so an off-screen miss
            // does not draw a phantom line into nothing.
            if (a.x < 0 || a.x >= MAP_COLUMNS || a.y < 0 || a.y >= MAP_ROWS ||
                b.x < 0 || b.x >= MAP_COLUMNS || b.y < 0 || b.y >= MAP_ROWS) {
                continue;
            }
            const int ax = a.x * CELL_SIZE + CELL_SIZE / 2;
            const int ay = a.y * CELL_SIZE + CELL_SIZE / 2;
            const int bx = b.x * CELL_SIZE + CELL_SIZE / 2;
            const int by = b.y * CELL_SIZE + CELL_SIZE / 2;
            DrawLineEx(Vector2{ static_cast<float>(ax),
                                static_cast<float>(ay) },
                       Vector2{ static_cast<float>(bx),
                                static_cast<float>(by) },
                       FIRE_TRAIL_THICKNESS,
                       trailColor);
        }

        // Impact flash: a coloured square on the final cell. A hit draws a
        // bright RED background plus a hot YELLOW core circle so the impact
        // reads unmistakably as "you struck"; a miss draws a dull gray puff
        // so the player sees the shot stop short without confusing it with
        // a hit.
        if (fireImpactCell_.x >= 0 && fireImpactCell_.x < MAP_COLUMNS &&
            fireImpactCell_.y >= 0 && fireImpactCell_.y < MAP_ROWS) {
            const int impactPixelX = fireImpactCell_.x * CELL_SIZE;
            const int impactPixelY = fireImpactCell_.y * CELL_SIZE;
            if (fireTrailHit_) {
                // Solid red square first, then a yellow core circle on top
                // for a vivid red+yellow flash that contrasts sharply with
                // any nearby miss puff.
                DrawRectangle(impactPixelX, impactPixelY,
                              CELL_SIZE, CELL_SIZE,
                              COLOR_FIRE_IMPACT_HIT);
                DrawCircle(impactPixelX + CELL_SIZE / 2,
                           impactPixelY + CELL_SIZE / 2,
                           static_cast<float>(FIRE_IMPACT_HIT_CORE_RADIUS),
                           COLOR_FIRE_IMPACT_HIT_FLASH);
            } else {
                // Dull gray smoke puff for a miss; intentionally dim so the
                // player can tell the shot didn't connect without reading
                // the event log.
                DrawRectangle(impactPixelX, impactPixelY,
                              CELL_SIZE, CELL_SIZE,
                              COLOR_FIRE_IMPACT_MISS);
            }
        }

        // Age the effect by one rendered frame; once it hits zero the trail
        // is no longer drawn (and the cached cells are simply ignored on the
        // next call until showFireEffect is invoked again).
        --fireTrailFramesRemaining_;
    }

    // ---- Transient enemy-attack effects (R14 visual cue) -----------------
    //
    // Draw every active enemy-attack cue, then age and prune the list. Ranged
    // attacks draw an orange beam from the attacker's tile to the player's
    // tile plus a small burst at the player end; melee attacks draw a red
    // slash flash over the player's tile. Drawing AFTER the player highlight
    // means the cue sits on top of the hero so an incoming hit is unmistakable.
    for (ActiveEnemyAttack& attack : enemyAttacks_) {
        if (attack.framesRemaining <= 0) {
            continue; // Expired entries are pruned below; skip drawing them.
        }

        const Vec2& ep = attack.enemyPos;
        const Vec2& pp = attack.playerPos;

        // Player-tile pixel centre is the common target / flash anchor.
        const bool playerOnScreen =
            pp.x >= 0 && pp.x < MAP_COLUMNS && pp.y >= 0 && pp.y < MAP_ROWS;

        if (attack.ranged) {
            // Beam: connect the attacker's cell centre to the player's cell
            // centre, but only when both ends are inside the visible map so
            // an off-screen attacker does not draw a phantom line.
            const bool enemyOnScreen =
                ep.x >= 0 && ep.x < MAP_COLUMNS && ep.y >= 0 && ep.y < MAP_ROWS;
            if (enemyOnScreen && playerOnScreen) {
                const int ex = ep.x * CELL_SIZE + CELL_SIZE / 2;
                const int ey = ep.y * CELL_SIZE + CELL_SIZE / 2;
                const int px = pp.x * CELL_SIZE + CELL_SIZE / 2;
                const int py = pp.y * CELL_SIZE + CELL_SIZE / 2;
                // A hit draws a bright orange beam plus a burst at the player;
                // a MISS (Feature 3) draws a dim, duller beam and no burst so
                // the player can read that the shot whiffed.
                const Color beamColor = attack.hit ? COLOR_ENEMY_BEAM
                                                    : COLOR_ENEMY_BEAM_MISS;
                DrawLineEx(Vector2{ static_cast<float>(ex),
                                    static_cast<float>(ey) },
                           Vector2{ static_cast<float>(px),
                                    static_cast<float>(py) },
                           ENEMY_BEAM_THICKNESS,
                           beamColor);
                if (attack.hit) {
                    DrawCircle(px, py,
                               static_cast<float>(ENEMY_BEAM_IMPACT_RADIUS),
                               COLOR_ENEMY_BEAM_IMPACT);
                }
            }
        } else if (playerOnScreen) {
            // Melee: a translucent red square stamped over the player's tile.
            DrawRectangle(pp.x * CELL_SIZE, pp.y * CELL_SIZE,
                          CELL_SIZE, CELL_SIZE,
                          COLOR_ENEMY_MELEE_FLASH);
        }

        // Age this cue by one rendered frame.
        --attack.framesRemaining;
    }

    // Drop every effect whose countdown has reached zero so the list does not
    // grow without bound across many turns of combat.
    {
        std::size_t writeIndex = 0;
        for (std::size_t i = 0; i < enemyAttacks_.size(); ++i) {
            if (enemyAttacks_[i].framesRemaining > 0) {
                if (writeIndex != i) {
                    enemyAttacks_[writeIndex] = enemyAttacks_[i];
                }
                ++writeIndex;
            }
        }
        enemyAttacks_.resize(writeIndex);
    }

    // ---- Transient Nova ultimate shockwave (Fix 2) -----------------------
    //
    // Drawn over the map (after entities) so the blast visibly washes over the
    // arena. renderNovaEffect ages its own frame counter each call.
    renderNovaEffect();

    // ---- Transient player melee slash (Fix 4) ----------------------------
    //
    // While the countdown is positive we draw a bright red "X" (two crossing
    // diagonal lines) centred on the target cell so the player gets visual
    // feedback that their melee attack landed. Drawn AFTER the player/enemy
    // rendering so the slash sits on top of the target glyph.
    if (meleeEffectFramesRemaining_ > 0) {
        const Vec2& mc = meleeEffectCell_;
        if (mc.x >= 0 && mc.x < MAP_COLUMNS &&
            mc.y >= 0 && mc.y < MAP_ROWS) {
            // Compute the four corner endpoints of the two diagonal lines,
            // inset from the cell edges by MELEE_SLASH_INSET pixels so the "X"
            // fits cleanly inside the cell.
            const float x1 = static_cast<float>(mc.x * CELL_SIZE + MELEE_SLASH_INSET);
            const float y1 = static_cast<float>(mc.y * CELL_SIZE + MELEE_SLASH_INSET);
            const float x2 = static_cast<float>((mc.x + 1) * CELL_SIZE - MELEE_SLASH_INSET);
            const float y2 = static_cast<float>((mc.y + 1) * CELL_SIZE - MELEE_SLASH_INSET);

            // Two crossing diagonal lines form the "X" slash.
            DrawLineEx(Vector2{x1, y1}, Vector2{x2, y2},
                       MELEE_SLASH_THICKNESS, COLOR_PLAYER_MELEE_SLASH);
            DrawLineEx(Vector2{x2, y1}, Vector2{x1, y2},
                       MELEE_SLASH_THICKNESS, COLOR_PLAYER_MELEE_SLASH);
        }
        // Age the effect by one rendered frame.
        --meleeEffectFramesRemaining_;
    }

    // ---- Hell mode overlay -----------------------------------------------
    //
    // Drawn after every other map-area effect so the red vignette / embers /
    // banner sit on top of the dungeon and entities, but BEFORE the HUD panel
    // and controls strip so the right-side panel stays clean and readable.
    renderHellMode();

    // ---- HUD panel and controls strip ------------------------------------
    renderHud();
    renderControlsStrip();

    // ---- Transient on-screen notice (Fix 1) ------------------------------
    //
    // Drawn last so it floats on top of everything (map + HUD). Used for the
    // "FIRE ON COOLDOWN" feedback when the player presses F while cooling down.
    renderTransientNotice();
}

// =============================================================================
// renderHud - the right-side status panel
// =============================================================================

/// Draw the right-hand HUD panel: background, header line, HP bar with text,
/// ammo, shield, charge bar with text, abilities list, recent-events block.
/// All vertical positions cascade from a running cursor (`y`) so adding or
/// removing a row only edits one place.
void RaylibRenderer::renderHud() const
{
    const GameState& state  = *cachedState_;
    const Config&    config = *cachedConfig_;
    const EventLog&  log    = *cachedLog_;
    const Player&    p      = state.player();

    // ---- Background panel + separator border on the left edge ------------
    DrawRectangle(HUD_PANEL_X, 0,
                  HUD_PANEL_WIDTH, HUD_PANEL_HEIGHT,
                  COLOR_HUD_BG);
    DrawRectangle(HUD_PANEL_X, 0, 2, HUD_PANEL_HEIGHT, COLOR_HUD_BORDER);

    // Running text cursor inside the panel; advanced by HUD_LINE_HEIGHT
    // after each row so the layout flows top-to-bottom.
    int textX = HUD_PANEL_X + HUD_PADDING;
    int y     = HUD_PADDING;

    // Reusable scratch buffer for short formatted lines (snprintf is enough
    // here; we avoid stringstream for HUD perf and to keep the code compact).
    char line[64];

    // ---- Header: Wave + Score -------------------------------------------
    std::snprintf(line, sizeof(line), "Wave: %d   Score: %d",
                  state.waveNumber(), state.score());
    DrawText(line, textX, y, HUD_FONT_SIZE, COLOR_TEXT);
    y += HUD_LINE_HEIGHT;

    std::snprintf(line, sizeof(line), "Turn: %d   Kills: %d",
                  state.turnCount(), state.enemiesKilled());
    DrawText(line, textX, y, HUD_FONT_SIZE, COLOR_TEXT_DIM);
    y += HUD_LINE_HEIGHT + HUD_PADDING / 2;

    // ---- HP bar + label --------------------------------------------------
    {
        const int   hp     = p.health();
        const int   hpMax  = (p.maxHealth() > 0) ? p.maxHealth() : 1;
        const float ratio  = std::clamp(static_cast<float>(hp) /
                                        static_cast<float>(hpMax), 0.0f, 1.0f);
        const int   filled = static_cast<int>(HUD_BAR_WIDTH * ratio);
        const Color fillColor = hpBarColorFor(ratio);

        // Empty bar background, then filled portion on top.
        DrawRectangle(textX, y, HUD_BAR_WIDTH, HUD_BAR_HEIGHT, COLOR_BAR_BG);
        DrawRectangle(textX, y, filled, HUD_BAR_HEIGHT, fillColor);
        // Subtle outline so the bar's full extent is always visible.
        DrawRectangleLines(textX, y, HUD_BAR_WIDTH, HUD_BAR_HEIGHT,
                           COLOR_HUD_BORDER);
        y += HUD_BAR_HEIGHT + 2;

        std::snprintf(line, sizeof(line), "HP: %d/%d", hp, p.maxHealth());
        DrawText(line, textX, y, HUD_FONT_SIZE, COLOR_TEXT);
        y += HUD_LINE_HEIGHT;
    }

    // ---- Armor line (Fix 2: visible feedback for Armor pickups) -----------
    //
    // Shows the player's current armor value so picking up an Armor item has
    // visible effect in the HUD. Light blue when armor > 0 (active protection),
    // dim gray when armor == 0 (no damage reduction). Placed between HP and AMO
    // because it is a defensive stat that logically groups with Health.
    {
        const int armorVal = p.armor();
        if (armorVal > 0) {
            std::snprintf(line, sizeof(line), "ARMOR: %d", armorVal);
            DrawText(line, textX, y, HUD_FONT_SIZE, COLOR_ARMOR);
        } else {
            DrawText("ARMOR: 0", textX, y, HUD_FONT_SIZE, COLOR_TEXT_DIM);
        }
        y += HUD_LINE_HEIGHT;
    }

    // ---- Ammo line -------------------------------------------------------
    std::snprintf(line, sizeof(line), "AMO: %d", p.ammo());
    DrawText(line, textX, y, HUD_FONT_SIZE, COLOR_TEXT);
    y += HUD_LINE_HEIGHT;

    // ---- Fire range line (Feature 1) ------------------------------------
    //
    // Reads the player's fireRange() directly so the HUD always reflects the
    // current reach, including any extension granted by a ranged Weapon pickup
    // (which calls Player::addFireRange on equip). Sits beside the AMO/FIRE
    // lines so the three ranged-combat stats read together.
    std::snprintf(line, sizeof(line), "RANGE: %d", p.fireRange());
    DrawText(line, textX, y, HUD_FONT_SIZE, COLOR_TEXT);
    y += HUD_LINE_HEIGHT;

    // ---- Gold reserve (Feature 1) ---------------------------------------
    //
    // Spendable currency awarded by Treasure pickups (in addition to Score).
    // Drawn in a bright gold colour so the player can read their wallet at a
    // glance before entering the post-wave Shop (Feature 2). Sits directly
    // under RANGE so all per-run resource lines (AMO/RANGE/GOLD) cluster.
    {
        constexpr Color COLOR_GOLD = { 255, 210, 60, 255 };
        std::snprintf(line, sizeof(line), "GOLD: %d", state.gold());
        DrawText(line, textX, y, HUD_FONT_SIZE, COLOR_GOLD);
        y += HUD_LINE_HEIGHT;
    }

    // ---- Fire cooldown bar + label (restored — 2-turn cooldown is active) ---
    //
    // The fire cooldown is back at 2 turns so each shot carries tactical weight.
    // The bar fills proportionally as the cooldown ticks toward 0: when ready
    // (cooldown == 0) the bar is fully green + "FIRE: READY"; while cooling
    // down the bar shows orange fill proportional to elapsed fraction plus a
    // "FIRE: cooldown N" label. If fireCooldownDuration is 0 (the "Rapid Fire"
    // upgrade card zeroes it out), always show READY regardless of the
    // fireCooldown() value so the bar never reads stale after an upgrade.
    {
        const int cooldownRemaining = p.fireCooldown();
        const int cooldownDuration  = p.fireCooldownDuration();

        // Bar background (same as HP bar).
        DrawRectangle(textX, y, HUD_BAR_WIDTH, HUD_BAR_HEIGHT, COLOR_BAR_BG);

        if (cooldownDuration <= 0 || cooldownRemaining <= 0) {
            // ---- READY state: full green bar, "FIRE: READY" label --------
            DrawRectangle(textX, y, HUD_BAR_WIDTH, HUD_BAR_HEIGHT,
                          COLOR_FIRE_READY);
            DrawRectangleLines(textX, y, HUD_BAR_WIDTH, HUD_BAR_HEIGHT,
                               COLOR_HUD_BORDER);
            y += HUD_BAR_HEIGHT + 2;
            DrawText("FIRE: READY", textX, y, HUD_FONT_SIZE, COLOR_FIRE_READY);
        } else {
            // ---- COOLDOWN state: orange bar fills as cooldown elapses -----
            // Fill ratio: how much of the cooldown has ELAPSED (not remaining).
            // elapsed = duration - remaining; ratio = elapsed / duration.
            // This means the bar starts nearly empty after a shot and fills to
            // full as the cooldown expires — matching the intuition "bar full =
            // ability ready" that HP and charge bars already teach the player.
            const float elapsed = static_cast<float>(cooldownDuration - cooldownRemaining);
            const float ratio   = std::clamp(
                elapsed / static_cast<float>(cooldownDuration), 0.0f, 1.0f);
            const int filled = static_cast<int>(HUD_BAR_WIDTH * ratio);

            DrawRectangle(textX, y, filled, HUD_BAR_HEIGHT,
                          COLOR_FIRE_COOLDOWN);
            DrawRectangleLines(textX, y, HUD_BAR_WIDTH, HUD_BAR_HEIGHT,
                               COLOR_HUD_BORDER);
            y += HUD_BAR_HEIGHT + 2;
            std::snprintf(line, sizeof(line), "FIRE: cooldown %d",
                          cooldownRemaining);
            DrawText(line, textX, y, HUD_FONT_SIZE, COLOR_FIRE_COOLDOWN);
        }
        y += HUD_LINE_HEIGHT;
    }

    // ---- Shield status ---------------------------------------------------
    if (p.isShielded()) {
        std::snprintf(line, sizeof(line), "SHL: ON [%d turns]",
                      p.shieldRemainingTurns());
        DrawText(line, textX, y, HUD_FONT_SIZE, COLOR_HP_HIGH);
    } else {
        DrawText("SHL: Off", textX, y, HUD_FONT_SIZE, COLOR_TEXT_DIM);
    }
    y += HUD_LINE_HEIGHT + HUD_PADDING / 2;

    // ---- Charge bar + label (Nova ultimate resource, made unmistakable) --
    {
        const int   charge    = p.chargeMeter();
        const int   chargeMax = (config.chargeMeterMax() > 0)
                                 ? config.chargeMeterMax() : 1;
        const float ratio  = std::clamp(static_cast<float>(charge) /
                                        static_cast<float>(chargeMax),
                                        0.0f, 1.0f);
        const int   filled = static_cast<int>(HUD_BAR_WIDTH * ratio);

        // The meter is FULL when the player's charge has reached the configured
        // maximum — this is the moment Nova becomes usable (R22.3).
        const bool isFull = (charge >= config.chargeMeterMax());

        // Draw the (taller) empty background first using the dedicated darker
        // charge background so even a sliver of purple fill is readable.
        DrawRectangle(textX, y, HUD_BAR_WIDTH, HUD_CHARGE_BAR_HEIGHT,
                      COLOR_CHARGE_BG);

        // Choose the fill colour. When full we PULSE between two bright
        // magentas using a time-based sine so the gauge shimmers; otherwise we
        // draw the steady purple fill scaled to the current ratio.
        if (isFull) {
            // sine in [-1,1] → blend factor in [0,1] for the pulse.
            const float pulse =
                0.5f + 0.5f * static_cast<float>(
                    std::sin(GetTime() * CHARGE_PULSE_SPEED));
            const Color fullFill =
                lerpColor(COLOR_CHARGE_FULL, COLOR_CHARGE_FULL_PULSE, pulse);
            DrawRectangle(textX, y, HUD_BAR_WIDTH, HUD_CHARGE_BAR_HEIGHT,
                          fullFill);
        } else {
            DrawRectangle(textX, y, filled, HUD_CHARGE_BAR_HEIGHT,
                          COLOR_CHARGE);
        }

        // Bright, thick outline so the bar reads as a deliberate gauge and its
        // full extent is always visible against the panel.
        for (int t = 0; t < HUD_CHARGE_BAR_OUTLINE; ++t) {
            DrawRectangleLines(textX - t, y - t,
                               HUD_BAR_WIDTH + 2 * t,
                               HUD_CHARGE_BAR_HEIGHT + 2 * t,
                               COLOR_CHARGE_OUTLINE);
        }
        y += HUD_CHARGE_BAR_HEIGHT + HUD_CHARGE_BAR_OUTLINE + 2;

        // Label: a vivid call-to-action when full, otherwise the x/100 readout.
        if (isFull) {
            DrawText("CHG: FULL - press 2 for NOVA!",
                     textX, y, HUD_FONT_SIZE, COLOR_CHARGE_FULL_TEXT);
        } else {
            std::snprintf(line, sizeof(line), "CHG: %d/%d",
                          charge, config.chargeMeterMax());
            DrawText(line, textX, y, HUD_FONT_SIZE, COLOR_TEXT);
        }
        y += HUD_LINE_HEIGHT + HUD_PADDING / 2;
    }

    // ---- Abilities list (one per line) ----------------------------------
    DrawText("Abilities:", textX, y, HUD_FONT_SIZE, COLOR_TEXT_DIM);
    y += HUD_LINE_HEIGHT;

    const auto& abilities = p.abilities();
    if (abilities.empty()) {
        DrawText("  (none)", textX, y, HUD_FONT_SIZE, COLOR_TEXT_DIM);
        y += HUD_LINE_HEIGHT;
    } else {
        // Nova is gated by a FULL Charge_Meter rather than a cooldown, so its
        // HUD entry needs special wording: "READY!" in vivid magenta when the
        // meter is full, or "needs full Charge: x/100" otherwise. Computed once
        // here so the per-ability loop below can branch on it.
        const int  charge   = p.chargeMeter();
        const int  chargeMax = config.chargeMeterMax();
        const bool novaReady = (charge >= chargeMax);

        for (const auto& abilPtr : abilities) {
            if (!abilPtr) { continue; }
            const char* label = abilityLabel(abilPtr->kind());

            // ---- Nova: charge-gated, not cooldown-gated ------------------
            if (abilPtr->kind() == AbilityKind::Nova) {
                if (novaReady) {
                    std::snprintf(line, sizeof(line), "%s READY!", label);
                    DrawText(line, textX, y, HUD_FONT_SIZE,
                             COLOR_NOVA_READY_TEXT);
                } else {
                    std::snprintf(line, sizeof(line),
                                  "%s (needs full Charge: %d/%d)",
                                  label, charge, chargeMax);
                    DrawText(line, textX, y, HUD_FONT_SIZE, COLOR_TEXT_DIM);
                }
                y += HUD_LINE_HEIGHT;
                continue;
            }

            // ---- All other abilities: cooldown-gated as before -----------
            if (abilPtr->isReady()) {
                std::snprintf(line, sizeof(line), "%s (RDY)", label);
                DrawText(line, textX, y, HUD_FONT_SIZE, COLOR_HP_HIGH);
            } else {
                std::snprintf(line, sizeof(line), "%s (%d)", label,
                              abilPtr->cooldownRemaining());
                DrawText(line, textX, y, HUD_FONT_SIZE, COLOR_TEXT_DIM);
            }
            y += HUD_LINE_HEIGHT;
        }
    }

    y += HUD_PADDING / 2;

    // ---- Ability keys cheat-sheet (sits above the LEGEND) ---------------
    //
    // User feedback after the first build: the bare ability list told the
    // player which key to press but not what each ability needs (Dash needs
    // a direction, Nova needs a full Charge meter, Shield/Blink fire alone).
    // Spelling each contract out here means the player never has to read
    // the source to know what 1/2/3/4 do. The header colour is a distinct
    // orange so the panel stands apart from the LEGEND header below.
    DrawText("ABILITY KEYS",
             textX, y,
             ABILITY_KEYS_HEADER_FONT_SIZE,
             COLOR_ABILITY_HEADER);
    y += ABILITY_KEYS_HEADER_FONT_SIZE + 4;

    for (int i = 0; i < ABILITY_KEY_LINE_COUNT; ++i) {
        DrawText(ABILITY_KEY_LINES[i].line,
                 textX, y,
                 HUD_FONT_SIZE,
                 COLOR_TEXT_DIM);
        y += HUD_FONT_SIZE + 4;
    }
    y += HUD_PADDING / 2;

    // ---- Legend (entity / item glyph -> meaning) -------------------------
    //
    // User feedback: enemies are visually similar at a glance and the @ alone
    // does not say "you" loudly enough. The legend pairs every glyph with a
    // colour swatch and short label so anyone joining mid-run can read the
    // map without consulting the source. Two-column layout keeps the legend
    // compact inside the 320 px HUD panel.
    DrawText("LEGEND", textX, y, LEGEND_HEADER_FONT_SIZE, COLOR_TEXT_HI);
    y += LEGEND_HEADER_FONT_SIZE + 4;

    {
        // Each entry is (glyph string, colour, label, drawAsCircle). Wrapping
        // these in a struct keeps the table readable; a flat tuple would also
        // work but would need parallel arrays which are easier to get wrong.
        // The drawAsCircle flag exists specifically for the "You" row: the
        // player on the map is drawn as a large blue DISC (see the player
        // highlight block in renderGameFrame), so the legend should match
        // that visual rather than showing a stray '@' glyph that no longer
        // mirrors what the player sees in-game.
        struct LegendEntry {
            const char* glyph;
            Color       color;
            const char* label;
            bool        drawAsCircle;
        };

        // Order is roughly "the things you'll see most often" first so a
        // skim-read teaches the most useful glyphs without needing to read
        // the full table.
        const LegendEntry entries[] = {
            { "@", COLOR_PLAYER_DISC, "You",              true  },
            { "M", COLOR_MELEE,       "Melee (adjacent)", false },
            { "R", COLOR_ROOK,        "Rook (rows/cols)", false },
            { "B", COLOR_BISHOP,      "Bishop (diag)",    false },
            { "Q", COLOR_QUEEN,       "Queen (any line)", false },
            { "F", COLOR_FAST,        "Fast (2 moves)",   false },
            { "X", COLOR_BOSS,        "BOSS",             false },
            { "!", COLOR_POTION,      "Potion (heal)",    false },
            { "/", COLOR_WEAPON,      "Weapon",           false },
            { "=", COLOR_AMMO,        "Ammo",             false },
            { "]", COLOR_ARMOR,       "Armor",            false },
            { "$", COLOR_TREASURE,    "Treasure",         false },
        };
        const int entryCount =
            static_cast<int>(sizeof(entries) / sizeof(entries[0]));

        // Two columns, each half the panel's inner width. Glyph is drawn in
        // its signature colour at the cell's left edge, with the label in
        // dim text immediately to its right.
        const int columnWidth = HUD_BAR_WIDTH / 2;
        const int rowsPerCol  = (entryCount + 1) / 2;

        // Radius of the small filled circle drawn for the "You" entry. Sized
        // so it fits inside the LEGEND_LINE_HEIGHT slot without overflowing.
        constexpr int LEGEND_PLAYER_CIRCLE_RADIUS = 5;

        for (int i = 0; i < entryCount; ++i) {
            const int col = i / rowsPerCol;          // 0 (left) or 1 (right).
            const int row = i % rowsPerCol;
            const int rowX = textX + col * columnWidth;
            const int rowY = y + row * LEGEND_LINE_HEIGHT;

            if (entries[i].drawAsCircle) {
                // Draw a small filled cyan/blue circle that mirrors the
                // on-map player disc so the legend is unmistakably "you".
                DrawCircle(rowX + LEGEND_PLAYER_CIRCLE_RADIUS,
                           rowY + LEGEND_ENTRY_FONT_SIZE / 2,
                           static_cast<float>(LEGEND_PLAYER_CIRCLE_RADIUS),
                           entries[i].color);
            } else {
                // Coloured glyph (one-character "swatch" + symbol in one).
                DrawText(entries[i].glyph,
                         rowX, rowY,
                         LEGEND_ENTRY_FONT_SIZE,
                         entries[i].color);
            }
            // Label immediately to the right of the glyph / circle cell.
            DrawText(entries[i].label,
                     rowX + LEGEND_ENTRY_FONT_SIZE + 4, rowY,
                     LEGEND_ENTRY_FONT_SIZE,
                     COLOR_TEXT_DIM);
        }

        y += rowsPerCol * LEGEND_LINE_HEIGHT + HUD_PADDING / 2;
    }

    // ---- Recent events block --------------------------------------------
    DrawText("RECENT EVENTS", textX, y, HUD_FONT_SIZE, COLOR_TEXT_HI);
    y += HUD_LINE_HEIGHT;

    // Cap the row count to whatever the panel still has space for, never
    // more than the configured display capacity nor the requested constant.
    const int requested  = std::min(HUD_EVENTS_TO_SHOW,
                                    config.eventLogDisplayCapacity());
    const std::vector<std::string> events = log.recent(requested);

    if (events.empty()) {
        DrawText("(no events yet)", textX, y, HUD_FONT_SIZE, COLOR_TEXT_DIM);
        y += HUD_LINE_HEIGHT;
    } else {
        for (const std::string& msg : events) {
            // If the message would overflow the panel, truncate visually with
            // an ellipsis so it never overlaps the window border.
            std::string display = msg;
            const int approxMaxChars = HUD_BAR_WIDTH / (HUD_FONT_SIZE / 2);
            if (static_cast<int>(display.size()) > approxMaxChars &&
                approxMaxChars > 3) {
                display = display.substr(0, approxMaxChars - 3) + "...";
            }
            DrawText(display.c_str(), textX, y, HUD_FONT_SIZE, COLOR_TEXT);
            y += HUD_LINE_HEIGHT;

            // Stop early if we've run past the bottom of the panel.
            if (y + HUD_LINE_HEIGHT > HUD_PANEL_HEIGHT - HUD_PADDING) {
                break;
            }
        }
    }
}

// =============================================================================
// renderControlsStrip - keybinding reminder under the map
// =============================================================================

/// Fill the strip beneath the map with the dark HUD background and print a
/// multi-line breakdown of every key the game understands. User feedback after
/// the first build reported the prior single-line summary was hard to skim;
/// the structured table below spells each binding out without growing the
/// window noticeably (only an extra 40 px of total height).
void RaylibRenderer::renderControlsStrip() const
{
    DrawRectangle(0, CONTROLS_STRIP_Y,
                  MAP_AREA_WIDTH, CONTROLS_STRIP_HEIGHT,
                  COLOR_HUD_BG);
    DrawRectangle(0, CONTROLS_STRIP_Y, MAP_AREA_WIDTH, 2, COLOR_HUD_BORDER);

    // Compute the total block height for vertical centring inside the strip.
    const int blockHeight = CONTROLS_LINE_COUNT * CONTROLS_LINE_HEIGHT;
    int y = CONTROLS_STRIP_Y + (CONTROLS_STRIP_HEIGHT - blockHeight) / 2;

    // Left margin: text aligned to a small inset rather than centred so the
    // category labels (MOVEMENT:, COMBAT:, ...) line up vertically and the
    // eye can scan straight down them. Inset matches the HUD's padding for
    // visual consistency with the right-hand panel.
    const int textX = HUD_PADDING;

    for (int i = 0; i < CONTROLS_LINE_COUNT; ++i) {
        DrawText(CONTROLS_LINES[i], textX, y,
                 CONTROLS_FONT_SIZE, COLOR_TEXT_DIM);
        y += CONTROLS_LINE_HEIGHT;
    }
}

// =============================================================================
// renderMenuOverlay - centred panel of selectable options
// =============================================================================

/// Draw the cached menu options inside a centred panel. Sized to fit the
/// largest option string and the row count. The selected entry is prefixed
/// with "> " and rendered in the highlight colour.
void RaylibRenderer::renderMenuOverlay() const
{
    if (menuOptions_.empty()) { return; }

    // Compute the panel's width from the widest option string so longer
    // shop entries (e.g. "Quickstep (25g) - next 4 turns: 2 moves per turn")
    // never overflow into the HUD on the right. We measure each option using
    // raylib's MeasureText, account for the "> " / "  " prefix and the panel
    // padding, and clamp the result to a sensible minimum and maximum so the
    // panel still has a clean look on short menus and never escapes the map
    // area on extreme ones.
    const int rowCount   = static_cast<int>(menuOptions_.size());
    constexpr int MENU_PREFIX_WIDTH = 24; // "> " / "  " plus a small safety margin.
    int widestText = 0;
    for (const std::string& opt : menuOptions_) {
        const int w = MeasureText(opt.c_str(), MENU_FONT_SIZE);
        if (w > widestText) { widestText = w; }
    }
    int panelW = widestText + MENU_PREFIX_WIDTH + 2 * OVERLAY_PADDING;
    if (panelW < MENU_PANEL_WIDTH) {
        panelW = MENU_PANEL_WIDTH;
    }
    // Hard cap: never wider than the map area minus a small breathing margin
    // so the panel cannot bleed into the right-side HUD even on extreme rows.
    const int kMenuMaxWidth = MAP_AREA_WIDTH - 2 * OVERLAY_PADDING;
    if (panelW > kMenuMaxWidth) {
        panelW = kMenuMaxWidth;
    }
    const int panelH     = rowCount * MENU_LINE_HEIGHT + 2 * OVERLAY_PADDING;
    const int panelX     = (WINDOW_WIDTH  - panelW) / 2;
    const int panelY     = (WINDOW_HEIGHT - panelH) / 2;

    // Translucent dark backdrop so the game frame underneath is dimmed but
    // still visible (helps the menus feel anchored to the world).
    DrawRectangle(panelX, panelY, panelW, panelH, COLOR_OVERLAY_PANEL);
    DrawRectangleLines(panelX, panelY, panelW, panelH, COLOR_HUD_BORDER);

    // Walk each option; prefix with "> " for the selected one.
    int y = panelY + OVERLAY_PADDING;
    for (int i = 0; i < rowCount; ++i) {
        const bool   isSelected = (i == menuSelected_);
        const Color  color      = isSelected ? COLOR_TEXT_HI : COLOR_TEXT;
        const std::string prefix = isSelected ? "> " : "  ";
        const std::string row    = prefix + menuOptions_[i];
        DrawText(row.c_str(),
                 panelX + OVERLAY_PADDING, y,
                 MENU_FONT_SIZE, color);
        y += MENU_LINE_HEIGHT;
    }
}

// =============================================================================
// renderMessageOverlay - stacked text near the top of the window
// =============================================================================

/// Draw the cached message lines inside a centred panel near the top of the
/// window. The panel auto-sizes to fit the line count.
void RaylibRenderer::renderMessageOverlay() const
{
    if (messageLines_.empty()) { return; }

    // Compute the widest line so the panel can be sized snugly around it.
    int widest = 0;
    for (const std::string& s : messageLines_) {
        const int w = MeasureText(s.c_str(), MESSAGE_FONT_SIZE);
        if (w > widest) { widest = w; }
    }

    const int panelW = std::min(WINDOW_WIDTH - 2 * OVERLAY_PADDING,
                                widest + 2 * OVERLAY_PADDING);
    const int panelH = static_cast<int>(messageLines_.size()) *
                       MESSAGE_LINE_HEIGHT + 2 * OVERLAY_PADDING;
    const int panelX = (WINDOW_WIDTH - panelW) / 2;
    const int panelY = MESSAGE_PANEL_Y;

    DrawRectangle(panelX, panelY, panelW, panelH, COLOR_OVERLAY_PANEL);
    DrawRectangleLines(panelX, panelY, panelW, panelH, COLOR_HUD_BORDER);

    int y = panelY + OVERLAY_PADDING;
    for (const std::string& s : messageLines_) {
        DrawText(s.c_str(),
                 panelX + OVERLAY_PADDING, y,
                 MESSAGE_FONT_SIZE, COLOR_TEXT);
        y += MESSAGE_LINE_HEIGHT;
    }
}

// =============================================================================
// renderFirePromptOverlay - centred prompt while waiting for a fire direction
// =============================================================================

/// Draw the fire-direction prompt at the BOTTOM of the window (Feature 2),
/// just above / within the controls strip, rather than centred over the map —
/// the map is now used to show the four firing-lane previews, so the prompt is
/// moved out of the way. Only invoked while waitForFireDirection is blocked on
/// a second key.
void RaylibRenderer::renderFirePromptOverlay() const
{
    // Updated text includes the "(R to cancel)" hint so the player always
    // knows they can back out of the fire prompt without wasting a turn.
    const char* text = "FIRE: choose direction (WASD)  (R to cancel)";

    const int textW = MeasureText(text, FIRE_PROMPT_FONT_SIZE);
    const int panelW = textW + 2 * OVERLAY_PADDING;
    const int panelH = FIRE_PROMPT_FONT_SIZE + 2 * OVERLAY_PADDING;
    // Horizontally centred over the map area, anchored to the bottom of the
    // window so it sits in the controls strip rather than over the play field.
    const int panelX = (MAP_AREA_WIDTH - panelW) / 2;
    const int panelY = WINDOW_HEIGHT - panelH - OVERLAY_PADDING;

    DrawRectangle(panelX, panelY, panelW, panelH, COLOR_OVERLAY_PANEL);
    DrawRectangleLines(panelX, panelY, panelW, panelH, COLOR_HUD_BORDER);
    DrawText(text,
             panelX + OVERLAY_PADDING,
             panelY + OVERLAY_PADDING,
             FIRE_PROMPT_FONT_SIZE,
             COLOR_TEXT_HI);
}

// =============================================================================
// computeFireLane - cells a shot would cover in one direction (Feature 2)
// =============================================================================

/// Trace the lane a fired projectile would sweep from `origin` along `dir`, up
/// to `range` cells, stopping at the first wall or out-of-bounds cell. The
/// origin itself is excluded; the returned cells run nearest-to-farthest. This
/// mirrors the CombatSystem's stopping rule (stop at wall / oob) closely enough
/// for a faithful preview while keeping the trace local to the renderer.
std::vector<Vec2> RaylibRenderer::computeFireLane(const Vec2& origin,
                                                  const Vec2& dir,
                                                  int range) const
{
    std::vector<Vec2> lane;
    if (cachedState_ == nullptr) { return lane; }

    const GridMap& map = cachedState_->map();
    Vec2 cell = origin;
    for (int step = 0; step < range; ++step) {
        cell = cell + dir;
        // Stop at the first wall or out-of-bounds cell — the shot cannot pass
        // it (matches CombatSystem's miss-on-wall/oob rule).
        if (!map.inBounds(cell) || map.typeAt(cell) == TileType::Wall) {
            break;
        }
        lane.push_back(cell);
    }
    return lane;
}

// =============================================================================
// renderFirePreview - draw the four firing lanes + targets (Feature 2)
// =============================================================================

/// Overlay the four firing-lane previews on the map while the player chooses a
/// fire direction. Each lane cell up to the player's fire range is painted a
/// semi-transparent yellow/orange pip; the FIRST enemy encountered in a lane is
/// painted red to show it would be hit. Lanes stop at walls / out-of-bounds via
/// computeFireLane, so the player sees exactly how far each shot reaches.
///
/// Spread-shot preview: when the player has a ranged weapon equipped (spread
/// shot active), each cardinal direction also shows TWO additional diagonal
/// lanes (45 degrees left and right) in a slightly dimmer hue so the player
/// can see the full three-beam coverage before committing.
void RaylibRenderer::renderFirePreview() const
{
    if (cachedState_ == nullptr) { return; }

    const GameState& state  = *cachedState_;
    const Vec2       origin = state.player().position();
    const int        range  = state.player().fireRange();
    const bool       spread = state.player().hasSpreadShot();

    // The four cardinal firing directions (4-directional movement model).
    const Vec2 directions[] = {
        Vec2(0, -1), // up
        Vec2(0, +1), // down
        Vec2(-1, 0), // left
        Vec2(+1, 0), // right
    };

    // Helper lambda: draw a single lane with the given highlight colour.
    auto drawLane = [&](const Vec2& dir, Color laneColor, Color targetColor) {
        const std::vector<Vec2> lane = computeFireLane(origin, dir, range);
        bool targetMarked = false;

        for (const Vec2& cell : lane) {
            if (cell.x < 0 || cell.x >= MAP_COLUMNS ||
                cell.y < 0 || cell.y >= MAP_ROWS) {
                continue;
            }

            bool enemyHere = false;
            if (!targetMarked) {
                for (const auto& enemyPtr : state.enemies()) {
                    if (enemyPtr && enemyPtr->position() == cell) {
                        enemyHere = true;
                        break;
                    }
                }
            }

            const int px = cell.x * CELL_SIZE + FIRE_PREVIEW_INSET;
            const int py = cell.y * CELL_SIZE + FIRE_PREVIEW_INSET;
            const int sz = CELL_SIZE - 2 * FIRE_PREVIEW_INSET;

            if (enemyHere) {
                DrawRectangle(px, py, sz, sz, targetColor);
                targetMarked = true;
                break; // First enemy stops the shot in this lane.
            }
            DrawRectangle(px, py, sz, sz, laneColor);
        }
    };

    // Dimmer colours for the spread-shot diagonal lanes so they're
    // distinguishable from the main lane but clearly part of the same shot.
    constexpr Color SPREAD_LANE_COLOR   = { 255, 180, 60, 55 };
    constexpr Color SPREAD_TARGET_COLOR = { 255, 80, 80, 120 };

    for (const Vec2& dir : directions) {
        // Main cardinal lane (original behaviour).
        drawLane(dir, COLOR_FIRE_PREVIEW_LANE, COLOR_FIRE_PREVIEW_TARGET);

        // Spread-shot bonus diagonal lanes (only when a ranged weapon is equipped).
        if (spread) {
            // Compute adjacent diagonal directions by rotating 45 degrees.
            // Left rotation: (dx+dy, dy-dx) clamped to {-1,0,1}.
            const int lx = (dir.x + dir.y > 0) ? 1 : (dir.x + dir.y < 0) ? -1 : 0;
            const int ly = (dir.y - dir.x > 0) ? 1 : (dir.y - dir.x < 0) ? -1 : 0;
            // Right rotation: (dx-dy, dy+dx) clamped to {-1,0,1}.
            const int rx = (dir.x - dir.y > 0) ? 1 : (dir.x - dir.y < 0) ? -1 : 0;
            const int ry = (dir.y + dir.x > 0) ? 1 : (dir.y + dir.x < 0) ? -1 : 0;

            const Vec2 leftDir(lx, ly);
            const Vec2 rightDir(rx, ry);

            if (!(leftDir.x == 0 && leftDir.y == 0)) {
                drawLane(leftDir, SPREAD_LANE_COLOR, SPREAD_TARGET_COLOR);
            }
            if (!(rightDir.x == 0 && rightDir.y == 0)) {
                drawLane(rightDir, SPREAD_LANE_COLOR, SPREAD_TARGET_COLOR);
            }
        }
    }
}

// =============================================================================
// renderDashPromptOverlay - centred prompt while waiting for a dash direction
// =============================================================================

/// Draw a small "Dash direction? (WASD)" panel at the BOTTOM of the window,
/// mirroring the relocated fire prompt (Feature 2) so both directional prompts
/// sit out of the way of the map. The label is the only difference so the
/// player can tell which command the game is waiting on (Dash needs a
/// direction; Nova/Shield/Blink do not). Only invoked while
/// waitForDashDirection is blocked on a second key.
void RaylibRenderer::renderDashPromptOverlay() const
{
    const char* text = "Dash direction? (WASD / Arrows)  (R to cancel)";

    const int textW = MeasureText(text, DASH_PROMPT_FONT_SIZE);
    const int panelW = textW + 2 * OVERLAY_PADDING;
    const int panelH = DASH_PROMPT_FONT_SIZE + 2 * OVERLAY_PADDING;
    const int panelX = (MAP_AREA_WIDTH - panelW) / 2;
    const int panelY = WINDOW_HEIGHT - panelH - OVERLAY_PADDING;

    DrawRectangle(panelX, panelY, panelW, panelH, COLOR_OVERLAY_PANEL);
    DrawRectangleLines(panelX, panelY, panelW, panelH, COLOR_HUD_BORDER);
    DrawText(text,
             panelX + OVERLAY_PADDING,
             panelY + OVERLAY_PADDING,
             DASH_PROMPT_FONT_SIZE,
             COLOR_ABILITY_HEADER);
}

// =============================================================================
// IRenderer override: drawFrame
// =============================================================================

/// Cache the supplied state pointers and render the resulting frame once.
/// After this call:
///   * gameFrameActive_ is true so subsequent renderCurrentScreen calls
///     (driven by pollInput's wait loop) will keep redrawing the same world.
///   * pendingReset_ from a prior poll is consumed via resetCompositionIfNeeded.
void RaylibRenderer::drawFrame(const GameState& state,
                               const Config&    config,
                               const EventLog&  log)
{
    resetCompositionIfNeeded();

    // Cache the parameters by ADDRESS so the wait loop can re-read them. The
    // caller's values must remain valid until the next pollInput returns,
    // which Game.cpp guarantees (state, config, and log are members of Game).
    cachedState_     = &state;
    cachedConfig_    = &config;
    cachedLog_       = &log;
    gameFrameActive_ = true;

    renderCurrentScreen();
}

// =============================================================================
// IRenderer override: drawMenu
// =============================================================================

/// Cache the menu options and selected index, then re-render so the menu is
/// visible immediately. Does NOT clear messageLines_ - showMainMenu calls
/// drawMessage(title), drawMenu(opts), drawMessage(footer) in sequence and
/// expects all three to appear together (the resetCompositionIfNeeded call
/// handles the per-iteration reset, not the per-method one).
void RaylibRenderer::drawMenu(const std::vector<std::string>& options,
                              int selectedIndex)
{
    resetCompositionIfNeeded();

    menuOptions_  = options;            // Deep-copy so we own the strings.
    menuSelected_ = selectedIndex;
    menuActive_   = !options.empty();   // Empty list -> nothing to show.

    renderCurrentScreen();
}

// =============================================================================
// IRenderer override: drawMessage
// =============================================================================

/// Append the message (split on '\n') to the cached overlay and re-render.
/// Multiple consecutive drawMessage calls stack their lines on the panel,
/// which is what showMainMenu and showGameOver rely on.
void RaylibRenderer::drawMessage(const std::string& message)
{
    resetCompositionIfNeeded();

    // Split on '\n' so each visual line is one element of messageLines_.
    std::istringstream in(message);
    std::string        line;
    while (std::getline(in, line)) {
        messageLines_.push_back(line);
    }

    renderCurrentScreen();
}

// =============================================================================
// IRenderer override: showFireEffect - cache projectile path for next frames
// =============================================================================

/// Snapshot the projectile's path so renderGameFrame can render a yellow
/// tracer line + impact flash for the next ~half second of frames. The effect
/// is purely visual - no game state is touched. Repeated calls overwrite the
/// previous snapshot and reset the countdown so a quick second shot replaces
/// the first cleanly.
void RaylibRenderer::showFireEffect(const std::vector<Vec2>& trailCells,
                                    const Vec2& impactCell,
                                    bool hit)
{
    // Deep-copy so we own the cells once the caller's TurnResult goes out of
    // scope. The trail is small (a handful of Vec2 entries at most) so the
    // copy cost is negligible.
    fireTrailCells_           = trailCells;
    fireImpactCell_           = impactCell;
    fireTrailHit_             = hit;
    fireTrailFramesRemaining_ = FIRE_EFFECT_FRAMES;

    // ---- Audio: fire sound (always) plus a short hit thump on impact ----
    // The fire sound plays for every shot fired; the additional hit thump
    // overlays it only when the projectile actually struck an enemy, so the
    // player hears the difference between a miss (just the woosh) and a hit
    // (woosh + thump). The two play simultaneously and raylib mixes them.
    if (audioReady_) {
        PlaySound(fireSound_);
        if (hit) {
            PlaySound(hitSound_);
        }
    }

    // No need to call renderCurrentScreen here: the next drawFrame call from
    // Game.cpp's playing loop will redraw the world and pick up the trail
    // through renderGameFrame, which decrements the counter each frame.
}

// =============================================================================
// IRenderer override: showEnemyAttackEffect - cache one incoming attack cue
// =============================================================================

/// Append one enemy-attack cue to the active list so renderGameFrame can draw
/// it (an orange beam for a ranged attacker, a red slash flash for a melee
/// attacker) over the next ENEMY_ATTACK_EFFECT_FRAMES rendered frames. The
/// effect is purely visual. We APPEND rather than overwrite so several enemies
/// attacking in the same turn each get their own visible cue.
void RaylibRenderer::showEnemyAttackEffect(const Vec2& enemyPos,
                                           const Vec2& playerPos,
                                           bool ranged,
                                           bool hit)
{
    ActiveEnemyAttack attack;
    attack.enemyPos        = enemyPos;
    attack.playerPos       = playerPos;
    attack.ranged          = ranged;
    attack.hit             = hit;
    attack.framesRemaining = ENEMY_ATTACK_EFFECT_FRAMES;
    enemyAttacks_.push_back(attack);

    // ---- Audio: enemy hit sound (only when the attack actually landed) --
    // A missed ranged shot still draws a beam (so the player sees they were
    // shot at) but should not play the impact thump because no damage was
    // taken. The audio cue mirrors the visual: hit -> sound, miss -> silence.
    if (audioReady_ && hit) {
        PlaySound(enemyHitSound_);
    }

    // Like showFireEffect, no immediate redraw is needed: the playing loop's
    // next drawFrame redraws the world and renderGameFrame picks up the new
    // entry, drawing and ageing it each frame until it expires.
}

// =============================================================================
// showNovaEffect - stash a Nova ultimate blast for a few dramatic frames
// =============================================================================

/// Record the blast centre + radius and start the frame countdown so
/// renderGameFrame draws the expanding shockwave for the next NOVA_EFFECT_FRAMES
/// frames. Purely visual; no game state is touched. A second Nova simply
/// overwrites the first and restarts the countdown.
void RaylibRenderer::showNovaEffect(const Vec2& center, int radius)
{
    novaCenter_          = center;
    novaRadius_          = radius;
    novaFramesRemaining_ = NOVA_EFFECT_FRAMES;
    // Audio: dramatic descending boom. Always plays whether the blast hit any
    // enemy or not (the visual draws unconditionally, the sound matches it).
    if (audioReady_) {
        PlaySound(novaSound_);
    }
    // No immediate redraw: the playing loop's next drawFrame redraws the world
    // and renderGameFrame picks up the blast through renderNovaEffect, which
    // decrements the countdown each frame.
}

// =============================================================================
// showPlayerMeleeEffect - cache a melee-attack target for a few frames (Fix 4)
// =============================================================================

/// Record the melee target cell and start the frame countdown so renderGameFrame
/// draws a bright red "X" slash over the cell for MELEE_EFFECT_FRAMES frames.
/// Purely visual; no game state is touched. A second melee overwrites the first
/// and restarts the countdown.
void RaylibRenderer::showPlayerMeleeEffect(const Vec2& targetCell)
{
    meleeEffectCell_            = targetCell;
    meleeEffectFramesRemaining_ = MELEE_EFFECT_FRAMES;
    // Audio: punchy melee strike. Single-shot SFX that overlays whatever else
    // is playing.
    if (audioReady_) {
        PlaySound(meleeSound_);
    }
    // No immediate redraw: the playing loop's next drawFrame picks it up.
}

// =============================================================================
// Audio-only IRenderer overrides (procedural SFX hooks, no visuals)
// =============================================================================

/// Play the pickup chime (two-note ascending sine). Gated on audioReady_ so a
/// missing audio device is harmless.
void RaylibRenderer::showPickupSound()
{
    if (audioReady_) {
        PlaySound(pickupSound_);
    }
}

/// Play the wave-clear stinger (ascending major chord). Triggered by Game.cpp
/// the moment a turn resolves with every enemy defeated.
void RaylibRenderer::showWaveClearedSound()
{
    if (audioReady_) {
        PlaySound(waveClearSound_);
    }
}

/// Play the game-over sting (descending minor chord). Triggered by Game.cpp
/// the moment the player's HP hits zero. Stacks the cackling villain laugh on
/// top of the chord so the death moment feels triumphant for the dungeon and
/// brutal for the hero.
void RaylibRenderer::showGameOverSound()
{
    if (audioReady_) {
        PlaySound(gameOverSound_);
        PlaySound(villainLaughSound_);
    }
}

/// Play the activation sound for the given ability kind. Each kind picks a
/// distinctive timbre so the player can recognise the ability by ear:
///   * Dash   — short swept-noise whoosh.
///   * Nova   — long dramatic boom (also covered by showNovaEffect, but calling
///              both is harmless and reinforces the ultimate's drama).
///   * Shield — sustained shimmering chord.
///   * Blink  — fast freq-sweep zap.
void RaylibRenderer::showAbilitySound(AbilityKind kind)
{
    if (!audioReady_) { return; }
    switch (kind) {
        case AbilityKind::Dash:   PlaySound(dashSound_);   break;
        case AbilityKind::Nova:   PlaySound(novaSound_);   break;
        case AbilityKind::Shield: PlaySound(shieldSound_); break;
        case AbilityKind::Blink:  PlaySound(blinkSound_);  break;
    }
}

// =============================================================================
// setBossMusicActive - start or stop the streamed boss-wave background track
// =============================================================================

/// Toggle the boss-wave OGG track. When `active` is true the track is started
/// (or seeked to the beginning if it had already finished a previous play) and
/// the procedural ambient drone is ducked so the two layers do not fight one
/// another. When `active` is false the track is stopped and the ambient drone
/// is restored to its normal volume.
///
/// Idempotent: calling it twice with the same value (e.g. several non-boss
/// waves in a row) does nothing extra. Safe to call on a renderer where the
/// audio device or the OGG file failed to load — every internal step is gated
/// on audioReady_ and bossMusicLoaded_, so the call falls through silently.
void RaylibRenderer::setBossMusicActive(bool active)
{
    if (!audioReady_ || !bossMusicLoaded_) {
        // No working audio or no boss track on disk: nothing to do.
        return;
    }

    if (active && !bossMusicPlaying_) {
        // Stop the normal-wave track first so the two streams never play
        // simultaneously (mutually exclusive: a wave is either boss or normal,
        // never both at once).
        if (normalMusicLoaded_ && normalMusicPlaying_) {
            StopMusicStream(normalMusic_);
            normalMusicPlaying_ = false;
        }

        // Always seek back to the beginning so each boss fight starts at the
        // top of the track even if a previous boss wave had advanced the
        // playhead. PlayMusicStream after a SeekMusicStream(0) is the canonical
        // raylib pattern for "restart from the start".
        SeekMusicStream(bossMusic_, 0.0f);
        // Re-apply the (master-scaled) volume on every start so a Settings
        // adjustment made between waves is honoured by the next playback.
        SetMusicVolume(bossMusic_, BOSS_MUSIC_VOLUME * masterMusicVolume_);
        PlayMusicStream(bossMusic_);
        bossMusicPlaying_ = true;

        // Duck the ambient procedural drone underneath the boss track.
        SetAudioStreamVolume(musicStream_, AUDIO_MUSIC_VOLUME_DUCKED * masterMusicVolume_);
    }
    else if (!active && bossMusicPlaying_) {
        StopMusicStream(bossMusic_);
        bossMusicPlaying_ = false;

        // Restore the ambient drone to its normal level only if the other
        // streamed track is also silent — otherwise leave it ducked because
        // the normal-wave track is still playing.
        if (!normalMusicPlaying_) {
            SetAudioStreamVolume(musicStream_, AUDIO_MUSIC_VOLUME * masterMusicVolume_);
        }
    }
}

// =============================================================================
// setNormalMusicActive - start or stop the normal-wave streamed track
// =============================================================================

/// Toggle the normal-wave OGG track. Mirrors setBossMusicActive's structure:
/// when starting, stop any other streamed track first so the two never overlap;
/// always seek to the beginning so each new wave kicks in fresh; duck the
/// procedural ambient pad underneath. Idempotent on repeated same-state calls.
void RaylibRenderer::setNormalMusicActive(bool active)
{
    if (!audioReady_ || !normalMusicLoaded_) {
        // No audio device or no normal-wave track on disk: nothing to do.
        return;
    }

    if (active && !normalMusicPlaying_) {
        // Stop the boss track first (mutual exclusion).
        if (bossMusicLoaded_ && bossMusicPlaying_) {
            StopMusicStream(bossMusic_);
            bossMusicPlaying_ = false;
        }

        SeekMusicStream(normalMusic_, 0.0f);
        // Re-apply the (master-scaled) volume on every start so a Settings
        // adjustment made between waves is honoured by the next playback.
        SetMusicVolume(normalMusic_, BOSS_MUSIC_VOLUME * masterMusicVolume_);
        PlayMusicStream(normalMusic_);
        normalMusicPlaying_ = true;

        // Duck the ambient drone so the action track is the dominant layer.
        SetAudioStreamVolume(musicStream_, AUDIO_MUSIC_VOLUME_DUCKED * masterMusicVolume_);
    }
    else if (!active && normalMusicPlaying_) {
        StopMusicStream(normalMusic_);
        normalMusicPlaying_ = false;

        // Restore the ambient drone only if no other streamed track is
        // currently playing.
        if (!bossMusicPlaying_) {
            SetAudioStreamVolume(musicStream_, AUDIO_MUSIC_VOLUME * masterMusicVolume_);
        }
    }
}

// =============================================================================
// setMasterMusicVolume - update the master volume scalar (Feature 4)
// =============================================================================

/// Cache the new master volume and immediately re-apply it to every playing
/// track so a Settings menu adjustment is audible without any restart. The
/// input is clamped to [0.0, 1.0]. Idempotent: re-pushing the same value is
/// cheap and safe (the underlying SetMusicVolume / SetAudioStreamVolume calls
/// are themselves trivial).
void RaylibRenderer::setMasterMusicVolume(float volume)
{
    // Clamp to the documented range so callers cannot push the renderer into
    // negative or super-unity multipliers (which would either silence audio
    // unnecessarily or risk clipping).
    if (volume < 0.0f) { volume = 0.0f; }
    if (volume > 1.0f) { volume = 1.0f; }
    masterMusicVolume_ = volume;

    // Re-apply the volume to every track that is currently loaded. We push
    // the value regardless of "playing" state so a track that starts later
    // (next wave) reads the right volume; raylib stores the cached value on
    // the Music struct itself.
    if (!audioReady_) {
        return;
    }

    SetAudioStreamVolume(musicStream_,
                         (bossMusicPlaying_ || normalMusicPlaying_)
                             ? AUDIO_MUSIC_VOLUME_DUCKED * masterMusicVolume_
                             : AUDIO_MUSIC_VOLUME * masterMusicVolume_);

    if (bossMusicLoaded_) {
        SetMusicVolume(bossMusic_, BOSS_MUSIC_VOLUME * masterMusicVolume_);
    }
    if (normalMusicLoaded_) {
        SetMusicVolume(normalMusic_, BOSS_MUSIC_VOLUME * masterMusicVolume_);
    }
}

// =============================================================================
// waitForAnyKey - block on Space / Enter / Q / ESC for the dead-screen
// =============================================================================

/// Inner frame-driven loop that returns the moment the player presses Space,
/// Enter, Q or ESC, or closes the window. Unlike pollInput this method
/// deliberately does NOT interpret game keys (F, 1-4, WASD) so the Game Over
/// overlay can never accidentally route the player into the fire / dash
/// direction prompts (the previous defect that made the screen feel frozen
/// when the user hit Space after death).
///
/// The cached scene is redrawn each frame via renderCurrentScreen so the
/// overlay stays visible and raylib's input queue keeps pumping. Setting
/// pendingReset_ on exit matches pollInput's contract so the next public
/// draw call after this returns clears the cached overlays cleanly.
void RaylibRenderer::waitForAnyKey()
{
    while (true) {
        // Window close (X button) ends the wait — caller treats this as a
        // confirmation just like Q.
        if (WindowShouldClose()) {
            pendingReset_ = true;
            return;
        }
        if (IsKeyPressed(KEY_SPACE)  ||
            IsKeyPressed(KEY_ENTER)  ||
            IsKeyPressed(KEY_Q)      ||
            IsKeyPressed(KEY_ESCAPE)) {
            pendingReset_ = true;
            return;
        }
        renderCurrentScreen();
    }
}

// =============================================================================
// showTransientNotice - post a brief bottom-centre message (Fix 1)
// =============================================================================

/// Store the message and reset its frame counter so renderTransientNotice draws
/// it near the bottom-centre of the window for NOTICE_EFFECT_FRAMES frames.
void RaylibRenderer::showTransientNotice(const std::string& text)
{
    transientNotice_       = text;
    transientNoticeFrames_ = NOTICE_EFFECT_FRAMES;
}

// =============================================================================
// renderNovaEffect - draw the expanding Nova shockwave (Fix 2)
// =============================================================================

/// Draw the dramatic Nova blast centred on novaCenter_ while novaFramesRemaining_
/// is positive: a translucent filled blast disc covering the radius, several
/// expanding concentric rings (cyan / electric blue / white), and a ring of
/// jagged electric-arc lines from the centre to the edge. The animation grows
/// with elapsed frames so the shockwave visibly EXPANDS, then ages its counter.
void RaylibRenderer::renderNovaEffect() const
{
    if (novaFramesRemaining_ <= 0) {
        return; // No active blast.
    }

    // Only draw when the blast centre is inside the visible map area.
    if (novaCenter_.x < 0 || novaCenter_.x >= MAP_COLUMNS ||
        novaCenter_.y < 0 || novaCenter_.y >= MAP_ROWS) {
        --novaFramesRemaining_;
        return;
    }

    // Pixel centre of the blast.
    const int cx = novaCenter_.x * CELL_SIZE + CELL_SIZE / 2;
    const int cy = novaCenter_.y * CELL_SIZE + CELL_SIZE / 2;

    // The blast's full pixel reach: a Chebyshev radius of N tiles spans N cells
    // out from the centre in every direction, so the maximum drawn radius is
    // novaRadius_ cells plus half a cell to reach the far cell's centre.
    const int maxPixelRadius = novaRadius_ * CELL_SIZE + CELL_SIZE / 2;

    // Animation progress in [0,1]: 0 at the first frame, approaching 1 as the
    // effect ages out. Drives the ring expansion so the shockwave grows.
    const float progress =
        1.0f - static_cast<float>(novaFramesRemaining_) /
               static_cast<float>(NOVA_EFFECT_FRAMES);

    // ---- Translucent filled blast disc -----------------------------------
    // Covers the whole area of effect at low alpha so the floor / glyphs
    // beneath stay visible while the blast washes over them.
    DrawCircle(cx, cy, static_cast<float>(maxPixelRadius), COLOR_NOVA_BLAST_FILL);

    // ---- Expanding concentric rings --------------------------------------
    // Each ring leads the one inside it; their radii grow with `progress` so
    // the whole set sweeps outward from the centre to the blast edge.
    for (int ring = 0; ring < NOVA_RING_COUNT; ++ring) {
        // Stagger each ring's phase so they trail one another as they expand.
        const float ringPhase =
            progress + static_cast<float>(ring) /
                       static_cast<float>(NOVA_RING_COUNT);
        // Wrap the phase into [0,1] so trailing rings re-emerge from the centre.
        const float wrapped = ringPhase - static_cast<float>(static_cast<int>(ringPhase));
        const float ringRadius = wrapped * static_cast<float>(maxPixelRadius);

        // Cycle the three electric colours across the rings for variety.
        Color ringColor;
        const int colorPick = ring % 3;
        if (colorPick == 0)      { ringColor = COLOR_NOVA_RING_CYAN; }
        else if (colorPick == 1) { ringColor = COLOR_NOVA_RING_BLUE; }
        else                     { ringColor = COLOR_NOVA_WHITE;     }

        // DrawRing draws an annulus; using inner≈outer gives a crisp ring line.
        DrawRing(Vector2{ static_cast<float>(cx), static_cast<float>(cy) },
                 ringRadius,
                 ringRadius + NOVA_RING_THICKNESS,
                 0.0f, 360.0f, 64, ringColor);
    }

    // ---- Jagged electric-arc lines ---------------------------------------
    // Radiate NOVA_ARC_COUNT arcs evenly around the circle, each broken into
    // small segments that jitter perpendicular to the radius for an electric,
    // lightning-like look. The arcs grow with `progress` alongside the rings.
    const float arcReach = static_cast<float>(maxPixelRadius) *
                           std::clamp(progress + 0.3f, 0.0f, 1.0f);
    constexpr float kTwoPi = 6.2831853f;
    for (int arc = 0; arc < NOVA_ARC_COUNT; ++arc) {
        const float angle = kTwoPi * static_cast<float>(arc) /
                            static_cast<float>(NOVA_ARC_COUNT);
        const float dirX = std::cos(angle);
        const float dirY = std::sin(angle);
        // Perpendicular unit vector for the zig-zag jitter.
        const float perpX = -dirY;
        const float perpY =  dirX;

        // Walk outward in NOVA_ARC_SEGMENTS steps, jittering each joint.
        float prevX = static_cast<float>(cx);
        float prevY = static_cast<float>(cy);
        for (int seg = 1; seg <= NOVA_ARC_SEGMENTS; ++seg) {
            const float along = arcReach * static_cast<float>(seg) /
                                static_cast<float>(NOVA_ARC_SEGMENTS);
            // Alternate the jitter sign per segment so the arc zig-zags; the
            // final segment lands cleanly on the radius (no jitter) so arcs end
            // at the blast edge. Uses GetTime so the jaggedness shimmers a touch.
            float jitter = 0.0f;
            if (seg < NOVA_ARC_SEGMENTS) {
                const float sign = (seg % 2 == 0) ? 1.0f : -1.0f;
                jitter = sign * static_cast<float>(NOVA_ARC_JITTER);
            }
            const float px = static_cast<float>(cx) + dirX * along + perpX * jitter;
            const float py = static_cast<float>(cy) + dirY * along + perpY * jitter;
            DrawLineEx(Vector2{ prevX, prevY },
                       Vector2{ px, py },
                       NOVA_ARC_THICKNESS,
                       COLOR_NOVA_WHITE);
            prevX = px;
            prevY = py;
        }
    }

    // Age the effect by one rendered frame.
    --novaFramesRemaining_;
}

// =============================================================================
// renderHellMode - draw the per-wave "HELL ON EARTH" overlay
// =============================================================================

/// Tracks the active wave number and renders the dramatic mood shift that
/// kicks in 8 seconds after each wave starts:
///   1. Detects a wave change by comparing the current wave to the cached
///      hellModeLastWaveNumber_ — on change, snapshots GetTime() and clears
///      hellModeActive_ so the next wave starts quiet again.
///   2. Computes elapsed-seconds-in-wave; once it crosses
///      HELL_MODE_TRIGGER_SECONDS, sets hellModeActive_.
///   3. While active, draws (in this order, lowest to highest layer):
///        - a pulsing red vignette darkening the map edges.
///        - upward-drifting glowing ember particles over the map area.
///        - a brief pulsing "HELL ON EARTH" banner at the top of the map.
///
/// Lazily populates hellEmbers_ on the first activation. The whole effect
/// stays inside the map area (does not bleed onto the HUD panel or controls
/// strip) so the gameplay surfaces remain readable.
void RaylibRenderer::renderHellMode() const
{
    if (cachedState_ == nullptr) { return; }

    // The wave-change detection and hellModeActive_ flag are managed at the
    // top of renderGameFrame so the tile palette swap and this overlay stay
    // in sync; here we only need to early-out when hell mode is not active.
    if (!hellModeActive_) {
        return;
    }
    const double now = GetTime();
    const double elapsed = now - hellModeWaveStartTime_;

    // ---- Lazy ember pool init --------------------------------------------
    if (!hellEmbersInitialised_) {
        hellEmbers_.resize(HELL_EMBER_COUNT);
        for (auto& ember : hellEmbers_) {
            // Spread the initial spawn across the bottom 60% of the map so
            // the screen is already populated when hell mode triggers; new
            // embers spawn at the bottom and rise upward from there.
            ember.x = static_cast<float>(GetRandomValue(0, MAP_AREA_WIDTH - 1));
            ember.y = static_cast<float>(
                GetRandomValue(MAP_AREA_HEIGHT * 4 / 10, MAP_AREA_HEIGHT - 1));
            const int vyRangeInt = static_cast<int>(
                (HELL_EMBER_VY_MAX - HELL_EMBER_VY_MIN) * 100.0f);
            ember.vy = -(HELL_EMBER_VY_MIN +
                          static_cast<float>(GetRandomValue(0, vyRangeInt)) / 100.0f);
            const int vxRangeInt = static_cast<int>(HELL_EMBER_VX_RANGE * 200.0f);
            ember.vx = static_cast<float>(GetRandomValue(0, vxRangeInt)) / 100.0f
                       - HELL_EMBER_VX_RANGE;
            ember.maxLife = static_cast<float>(GetRandomValue(
                HELL_EMBER_LIFE_MIN_FRAMES, HELL_EMBER_LIFE_MAX_FRAMES));
            ember.life    = ember.maxLife;
            // Mix between bright orange-red and deeper red so the swarm
            // doesn't look monochromatic.
            ember.r = static_cast<unsigned char>(GetRandomValue(220, 255));
            ember.g = static_cast<unsigned char>(GetRandomValue(40,  120));
            ember.b = static_cast<unsigned char>(GetRandomValue(0,    40));
        }
        hellEmbersInitialised_ = true;
    }

    // ---- Pulsing red vignette --------------------------------------------
    //
    // Implemented as four concentric rectangle outlines with decreasing alpha
    // toward the centre. Slow sine pulse modulates the peak alpha so the
    // borders breathe red. We draw on the MAP AREA only (HUD panel stays
    // unaffected).
    const float pulse =
        0.5f + 0.5f * std::sin(static_cast<float>(now) * 1.3f);
    const int peakAlpha = static_cast<int>(
        static_cast<float>(HELL_VIGNETTE_MAX_ALPHA) * (0.6f + 0.4f * pulse));

    // Number of vignette layers. More layers = smoother gradient, but each
    // one is a single rectangle outline so cost stays trivial.
    constexpr int VIGNETTE_LAYERS = 14;
    for (int i = 0; i < VIGNETTE_LAYERS; ++i) {
        const float t = static_cast<float>(i) /
                        static_cast<float>(VIGNETTE_LAYERS - 1);
        // Alpha grows linearly from 0 (innermost) to peakAlpha (outermost).
        const int alpha = static_cast<int>(static_cast<float>(peakAlpha) * t);
        const Color layerColor = {
            HELL_VIGNETTE_R, HELL_VIGNETTE_G, HELL_VIGNETTE_B,
            static_cast<unsigned char>(alpha)
        };
        // Inset progressively as i grows so layer 0 is the biggest rectangle
        // (along the map edges) and the inner ones tighten toward the centre.
        const int inset = (VIGNETTE_LAYERS - 1 - i) * 6;
        const int x = inset;
        const int y = inset;
        const int w = MAP_AREA_WIDTH  - 2 * inset;
        const int h = MAP_AREA_HEIGHT - 2 * inset;
        if (w > 0 && h > 0) {
            DrawRectangleLinesEx(
                Rectangle{ static_cast<float>(x), static_cast<float>(y),
                           static_cast<float>(w), static_cast<float>(h) },
                3.0f, layerColor);
        }
    }

    // ---- Upward-drifting embers ------------------------------------------
    for (auto& ember : hellEmbers_) {
        // Life-fade alpha: ember dims as life ticks down to 0, brightest
        // at maxLife. Quadratic easing keeps it bright most of its life
        // before fading sharply at the end.
        const float lifeRatio = ember.life / ember.maxLife;
        const float alphaF    = lifeRatio * lifeRatio;
        const unsigned char a = static_cast<unsigned char>(255.0f * alphaF);

        // Outer glow: a translucent disc twice the core radius for a soft
        // halo around each ember.
        const Color glowColor = { ember.r, ember.g, ember.b,
                                  static_cast<unsigned char>(a / 4) };
        DrawCircle(static_cast<int>(ember.x), static_cast<int>(ember.y),
                   static_cast<float>(HELL_EMBER_CORE_RADIUS * 2),
                   glowColor);

        // Bright core: small fully-coloured disc on top of the halo.
        const Color coreColor = { ember.r, ember.g, ember.b, a };
        DrawCircle(static_cast<int>(ember.x), static_cast<int>(ember.y),
                   static_cast<float>(HELL_EMBER_CORE_RADIUS),
                   coreColor);

        // Advance the ember.
        ember.x    += ember.vx;
        ember.y    += ember.vy;
        ember.life -= 1.0f;

        // Recycle when life expires or the ember leaves the map area.
        if (ember.life <= 0.0f ||
            ember.y   <  -HELL_EMBER_CORE_RADIUS ||
            ember.x   <  -HELL_EMBER_CORE_RADIUS ||
            ember.x   >  MAP_AREA_WIDTH + HELL_EMBER_CORE_RADIUS) {
            ember.x = static_cast<float>(GetRandomValue(0, MAP_AREA_WIDTH - 1));
            ember.y = static_cast<float>(MAP_AREA_HEIGHT - 1);
            const int vyRangeInt = static_cast<int>(
                (HELL_EMBER_VY_MAX - HELL_EMBER_VY_MIN) * 100.0f);
            ember.vy = -(HELL_EMBER_VY_MIN +
                          static_cast<float>(GetRandomValue(0, vyRangeInt)) / 100.0f);
            const int vxRangeInt = static_cast<int>(HELL_EMBER_VX_RANGE * 200.0f);
            ember.vx = static_cast<float>(GetRandomValue(0, vxRangeInt)) / 100.0f
                       - HELL_EMBER_VX_RANGE;
            ember.maxLife = static_cast<float>(GetRandomValue(
                HELL_EMBER_LIFE_MIN_FRAMES, HELL_EMBER_LIFE_MAX_FRAMES));
            ember.life    = ember.maxLife;
            ember.r = static_cast<unsigned char>(GetRandomValue(220, 255));
            ember.g = static_cast<unsigned char>(GetRandomValue(40,  120));
            ember.b = static_cast<unsigned char>(GetRandomValue(0,    40));
        }
    }

    // ---- "HELL ON EARTH" pulsing banner ----------------------------------
    //
    // Drawn for the first HELL_MODE_BANNER_FRAMES frames after activation so
    // it gives the player a clear announcement of the mode shift, then fades
    // out and never blocks gameplay text again.
    // Compute the same trigger threshold renderGameFrame used for THIS wave
    // so the banner countdown lines up exactly with the visual escalation.
    const bool   isBossWave =
        cachedState_->waveNumber() > 0 &&
        (cachedState_->waveNumber() % HELL_MODE_BOSS_WAVE_MODULUS) == 0;
    const double triggerSeconds =
        isBossWave ? HELL_MODE_TRIGGER_SECONDS_BOSS
                   : HELL_MODE_TRIGGER_SECONDS_NORMAL;
    const double sinceTrigger = elapsed - triggerSeconds;
    const int    bannerFramesElapsed = static_cast<int>(sinceTrigger * 60.0);
    if (bannerFramesElapsed >= 0 && bannerFramesElapsed < HELL_MODE_BANNER_FRAMES) {
        // Alpha: fully visible for the first half, then fades linearly.
        const float bannerProgress =
            static_cast<float>(bannerFramesElapsed) /
            static_cast<float>(HELL_MODE_BANNER_FRAMES);
        const float bannerAlphaF = (bannerProgress < 0.5f)
                                       ? 1.0f
                                       : (1.0f - (bannerProgress - 0.5f) * 2.0f);
        const unsigned char bgAlpha   =
            static_cast<unsigned char>(HELL_BANNER_BACKGROUND.a * bannerAlphaF);
        const unsigned char textAlpha =
            static_cast<unsigned char>(HELL_BANNER_TEXT_COLOR.a * bannerAlphaF);

        // Banner background: a wide horizontal strip across the upper third
        // of the map area.
        const int bannerY = MAP_AREA_HEIGHT / 3;
        const Color bgFaded = { HELL_BANNER_BACKGROUND.r,
                                HELL_BANNER_BACKGROUND.g,
                                HELL_BANNER_BACKGROUND.b,
                                bgAlpha };
        DrawRectangle(0, bannerY, MAP_AREA_WIDTH, HELL_BANNER_HEIGHT_PX, bgFaded);

        // Pulse the text size slightly so the banner feels alive.
        const float textPulse =
            1.0f + 0.05f * std::sin(static_cast<float>(now) * 6.0f);
        const int   fontSize =
            static_cast<int>(HELL_BANNER_FONT_SIZE * textPulse);
        const int   textWidth = MeasureText(HELL_BANNER_TEXT, fontSize);
        const int   textX     = (MAP_AREA_WIDTH - textWidth) / 2;
        const int   textY     = bannerY + (HELL_BANNER_HEIGHT_PX - fontSize) / 2;
        const Color textFaded = { HELL_BANNER_TEXT_COLOR.r,
                                  HELL_BANNER_TEXT_COLOR.g,
                                  HELL_BANNER_TEXT_COLOR.b,
                                  textAlpha };
        DrawText(HELL_BANNER_TEXT, textX, textY, fontSize, textFaded);
    }
}

// =============================================================================
// renderTransientNotice - draw the bottom-centre feedback message (Fix 1)
// =============================================================================

/// Draw transientNotice_ (when active) inside a dark panel near the bottom-
/// centre of the window, just above the controls strip, then age its counter.
void RaylibRenderer::renderTransientNotice() const
{
    if (transientNoticeFrames_ <= 0 || transientNotice_.empty()) {
        return; // Nothing to show.
    }

    const char* text = transientNotice_.c_str();
    const int textW  = MeasureText(text, NOTICE_FONT_SIZE);

    // Panel sized to the text plus padding, horizontally centred over the map
    // area and resting just above the controls strip.
    const int panelW = textW + 2 * NOTICE_PADDING;
    const int panelH = NOTICE_FONT_SIZE + 2 * NOTICE_PADDING;
    const int panelX = (MAP_AREA_WIDTH - panelW) / 2;
    const int panelY = CONTROLS_STRIP_Y - panelH - NOTICE_BOTTOM_MARGIN;

    DrawRectangle(panelX, panelY, panelW, panelH, COLOR_NOTICE_PANEL);
    DrawText(text,
             panelX + NOTICE_PADDING,
             panelY + NOTICE_PADDING,
             NOTICE_FONT_SIZE,
             COLOR_NOTICE_TEXT);

    // Age the notice by one rendered frame; clear it once it expires.
    --transientNoticeFrames_;
}

/// Block in a frame loop until the player presses a recognised key. The first
/// matching key is converted to an InputCommand and returned; the OS close
/// button (or ESC, which raylib treats as "close") returns Quit.
///
/// The mapping is documented in the header. Ctrl+S (Save) is checked BEFORE
/// plain S so the save shortcut takes priority over a southward move when the
/// user is also holding Left Control.
InputCommand RaylibRenderer::pollInput()
{
    while (true) {
        // Closing the window via the title-bar X returns Quit so the game
        // shuts down cleanly. WindowShouldClose also fires on KEY_ESCAPE,
        // which we want to treat as an exit.
        if (WindowShouldClose()) {
            pendingReset_ = true;
            return InputCommand(InputCommand::Type::Quit);
        }

        // ---- Save shortcut: Ctrl + S (checked BEFORE plain S) ------------
        if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) &&
            IsKeyPressed(KEY_S)) {
            pendingReset_ = true;
            return InputCommand(InputCommand::Type::Save);
        }

        // ---- Movement keys ------------------------------------------------
        if (IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP)) {
            pendingReset_ = true;
            return InputCommand(InputCommand::Type::Move, Vec2(0, -1));
        }
        if (IsKeyPressed(KEY_S) || IsKeyPressed(KEY_DOWN)) {
            pendingReset_ = true;
            return InputCommand(InputCommand::Type::Move, Vec2(0, +1));
        }
        if (IsKeyPressed(KEY_A) || IsKeyPressed(KEY_LEFT)) {
            pendingReset_ = true;
            return InputCommand(InputCommand::Type::Move, Vec2(-1, 0));
        }
        if (IsKeyPressed(KEY_D) || IsKeyPressed(KEY_RIGHT)) {
            pendingReset_ = true;
            return InputCommand(InputCommand::Type::Move, Vec2(+1, 0));
        }

        // ---- Fire (asks for a follow-up direction key) -------------------
        if (IsKeyPressed(KEY_F)) {
            // Defence in depth (Fix 1): if Fire is still on cooldown, do NOT
            // enter the fire-direction prompt at all. Instead post a brief
            // on-screen notice so the player gets immediate, unmistakable
            // feedback, consume the F press, and keep polling. The CombatSystem
            // still independently rejects a cooldown shot, but stopping here at
            // the UI level means the player never even reaches the aim prompt.
            if (cachedState_ != nullptr &&
                cachedState_->player().fireCooldown() > 0) {
                const int remaining = cachedState_->player().fireCooldown();
                showTransientNotice("FIRE ON COOLDOWN - " +
                                    std::to_string(remaining) +
                                    " turn(s) left");
                // Redraw so the notice appears immediately, then continue the
                // poll loop without returning a command (no turn is consumed).
                renderCurrentScreen();
                continue;
            }

            // Block the fire prompt when ammo is depleted (mirrors the cooldown
            // check above). Without this guard, pressing F with 0 ammo would
            // show the aim prompt and then CombatSystem would reject the shot,
            // wasting the player's time. This early-out gives instant feedback.
            if (cachedState_ != nullptr &&
                cachedState_->player().ammo() <= 0) {
                showTransientNotice("NO AMMO — can't fire!");
                renderCurrentScreen();
                continue;
            }

            // Spread-shot shortcut: if the player has a ranged weapon equipped
            // the shot becomes 360° omnidirectional in CombatSystem, so asking
            // the player to pick one of eight directions is meaningless — every
            // direction fires anyway. Skip the prompt entirely and emit a Fire
            // command with an arbitrary placeholder direction (North); the
            // 360° spread loop ignores the main direction and fires every beam
            // independently.
            if (cachedState_ != nullptr &&
                cachedState_->player().hasSpreadShot()) {
                pendingReset_ = true;
                return InputCommand(InputCommand::Type::Fire, Vec2(0, -1));
            }

            // waitForFireDirection runs its own inner frame loop and returns
            // the completed Fire command (or Quit if the window was closed
            // while we waited). If the player pressed ESC to cancel, the
            // cancelled flag is set and we loop back to await fresh input —
            // no turn is consumed and no shot is wasted.
            bool cancelled = false;
            InputCommand fired = waitForFireDirection(cancelled);
            if (cancelled) {
                // Player backed out of the fire prompt with ESC; show a brief
                // notice and return to the main input loop without acting.
                showTransientNotice("Fire cancelled.");
                renderCurrentScreen();
                continue;
            }
            pendingReset_ = true;
            return fired;
        }

        // ---- Abilities (1-4) ---------------------------------------------
        //
        // Only Dash (key 1) requires a follow-up direction key — the other
        // three abilities (Nova / Shield / Blink) activate immediately on
        // their own key with a {0,0} direction, which their effect helpers
        // ignore. Routing key 1 through waitForDashDirection mirrors the
        // F-key flow so the UX is consistent across "directional" actions.
        if (IsKeyPressed(KEY_ONE)) {
            bool cancelled = false;
            InputCommand dashed = waitForDashDirection(cancelled);
            if (cancelled) {
                // Player backed out of the dash prompt with ESC; show a brief
                // notice and return to the main input loop without acting.
                showTransientNotice("Dash cancelled.");
                renderCurrentScreen();
                continue;
            }
            pendingReset_ = true;
            return dashed;
        }
        if (IsKeyPressed(KEY_TWO)) {
            pendingReset_ = true;
            return InputCommand(InputCommand::Type::UseAbility, Vec2(0, 0), 1);
        }
        if (IsKeyPressed(KEY_THREE)) {
            pendingReset_ = true;
            return InputCommand(InputCommand::Type::UseAbility, Vec2(0, 0), 2);
        }
        if (IsKeyPressed(KEY_FOUR)) {
            pendingReset_ = true;
            return InputCommand(InputCommand::Type::UseAbility, Vec2(0, 0), 3);
        }

        // ---- Wait (Space / Enter) ----------------------------------------
        if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)) {
            pendingReset_ = true;
            return InputCommand(InputCommand::Type::Wait);
        }

        // ---- Quit (Q) ----------------------------------------------------
        if (IsKeyPressed(KEY_Q)) {
            pendingReset_ = true;
            return InputCommand(InputCommand::Type::Quit);
        }

        // No recognised key this frame: redraw the cached scene and wait one
        // more frame. renderCurrentScreen calls Begin/EndDrawing, which also
        // pumps the OS event queue so subsequent IsKeyPressed calls update.
        renderCurrentScreen();
    }
}

// =============================================================================
// waitForFireDirection - inner wait loop for the second key of a Fire command
// =============================================================================

/// After F is pressed, wait for one of WASD / arrow keys and return the
/// matching Fire command. While we wait the cached scene is redrawn each
/// frame with firePromptActive_ = true so the player sees the direction
/// prompt overlay. If the player presses R to cancel, `cancelled` is set
/// to true and the returned command should be ignored by the caller (no turn
/// is consumed). The "(R to cancel)" hint is shown in the prompt overlay.
/// @param cancelled  output flag set to true when the player aborts the shot.
/// @return a Fire InputCommand carrying the chosen direction, or a dummy
///         command when cancelled (caller must check the flag first).
InputCommand RaylibRenderer::waitForFireDirection(bool& cancelled)
{
    firePromptActive_ = true;
    cancelled = false;
    while (true) {
        if (WindowShouldClose()) {
            firePromptActive_ = false;
            cancelled = false; // Window close is a real Quit, not a cancel.
            return InputCommand(InputCommand::Type::Quit);
        }

        if (IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP)) {
            firePromptActive_ = false;
            return InputCommand(InputCommand::Type::Fire, Vec2(0, -1));
        }
        if (IsKeyPressed(KEY_S) || IsKeyPressed(KEY_DOWN)) {
            firePromptActive_ = false;
            return InputCommand(InputCommand::Type::Fire, Vec2(0, +1));
        }
        if (IsKeyPressed(KEY_A) || IsKeyPressed(KEY_LEFT)) {
            firePromptActive_ = false;
            return InputCommand(InputCommand::Type::Fire, Vec2(-1, 0));
        }
        if (IsKeyPressed(KEY_D) || IsKeyPressed(KEY_RIGHT)) {
            firePromptActive_ = false;
            return InputCommand(InputCommand::Type::Fire, Vec2(+1, 0));
        }

        // R key cancels the fire without consuming a turn or spending ammo.
        // R is used as the "Return/Back" key here because ESC triggers
        // WindowShouldClose() and opens the main menu, which conflicts with
        // using ESC as an in-prompt cancel. The caller sees `cancelled == true`
        // and loops back to the main input poll, so pressing F then R brings
        // the player back to the "waiting for input" state as if F was never
        // pressed.
        if (IsKeyPressed(KEY_R)) {
            firePromptActive_ = false;
            cancelled = true;
            return InputCommand(InputCommand::Type::Wait); // Dummy; ignored.
        }

        renderCurrentScreen();
    }
}

// =============================================================================
// waitForDashDirection - inner wait loop for the second key of Dash (key 1)
// =============================================================================

/// After 1 (Dash) is pressed, wait for one of WASD / arrow keys and return
/// the matching UseAbility command at index 0 (Dash) carrying the chosen
/// direction. The structure mirrors waitForFireDirection so both prompts
/// behave identically: same close-window-returns-Quit guarantee, same
/// "R means cancel" escape hatch. On cancel the `cancelled` output
/// flag is set so the caller can loop back without consuming a turn.
/// "(R to cancel)" is shown in the dash prompt overlay.
/// @param cancelled  output flag set to true when the player aborts the dash.
/// @return a UseAbility InputCommand for Dash with the chosen direction,
///         or a dummy command when cancelled / Quit if the window was closed.
InputCommand RaylibRenderer::waitForDashDirection(bool& cancelled)
{
    dashPromptActive_ = true;
    cancelled = false;
    while (true) {
        if (WindowShouldClose()) {
            dashPromptActive_ = false;
            cancelled = false; // Window close is a real Quit, not a cancel.
            return InputCommand(InputCommand::Type::Quit);
        }

        if (IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP)) {
            dashPromptActive_ = false;
            return InputCommand(InputCommand::Type::UseAbility, Vec2(0, -1), 0);
        }
        if (IsKeyPressed(KEY_S) || IsKeyPressed(KEY_DOWN)) {
            dashPromptActive_ = false;
            return InputCommand(InputCommand::Type::UseAbility, Vec2(0, +1), 0);
        }
        if (IsKeyPressed(KEY_A) || IsKeyPressed(KEY_LEFT)) {
            dashPromptActive_ = false;
            return InputCommand(InputCommand::Type::UseAbility, Vec2(-1, 0), 0);
        }
        if (IsKeyPressed(KEY_D) || IsKeyPressed(KEY_RIGHT)) {
            dashPromptActive_ = false;
            return InputCommand(InputCommand::Type::UseAbility, Vec2(+1, 0), 0);
        }

        // R key cancels the dash without consuming a turn or triggering the
        // ability cooldown. R is the "Return/Back" key because ESC triggers
        // WindowShouldClose() and opens the main menu. The caller sees
        // `cancelled == true` and loops back to the main input poll, so
        // pressing 1 then R is a clean no-op.
        if (IsKeyPressed(KEY_R)) {
            dashPromptActive_ = false;
            cancelled = true;
            return InputCommand(InputCommand::Type::Wait); // Dummy; ignored.
        }

        renderCurrentScreen();
    }
}

} // namespace dga
