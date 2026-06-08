// =============================================================================
// render/ConsoleRenderer.cpp
//
// Purpose:
//   Implements the ConsoleRenderer — the ASCII terminal renderer that satisfies
//   Requirements 28.3, 28.5, 28.6, 29.1, 29.3, and 4.5.
//
//   All I/O is done here (std::cout for output, _getch / cin for input). The
//   game core never touches I/O directly; it calls through the IRenderer
//   interface (R8.2). This file is therefore the ONLY place in the project where
//   std::cout or keyboard-reading functions appear (except main.cpp startup
//   messages, which are gone once Task 11.1 is wired in).
//
//   Platform strategy:
//     * Screen clear:  system("cls") on Windows; ANSI "\033[2J\033[H" elsewhere.
//     * Single-keyread: _getch() from <conio.h> on Windows; raw-terminal read
//       via tcsetattr() on POSIX.
//   Both branches compile under the same -Wall -Wextra pass; the unused branch
//   is #ifdef'd out cleanly.
// =============================================================================

#include "render/ConsoleRenderer.h"

// Standard-library headers used only in the .cpp (keep the .h lean).
#include <iostream>  // std::cout, std::cin — all terminal output.
#include <string>    // std::string — local string formatting.
#include <vector>    // std::vector — event-log retrieval.

// Platform-specific single-character input.
#ifdef _WIN32
#  include <conio.h>  // _getch() — Windows non-blocking keyread.
#else
#  include <termios.h>  // tcgetattr / tcsetattr — POSIX terminal mode.
#  include <unistd.h>   // STDIN_FILENO — raw-mode input file descriptor.
#endif

// Game-logic headers are already pulled in transitively through IRenderer.h
// (GameState, Config, EventLog, InputCommand), so we only need the entity
// headers to call glyph() on enemies and items.
#include "entities/Enemy.h"   // Enemy::glyph()
#include "entities/Player.h"  // Player::glyph(), health, ammo, etc.
#include "items/Item.h"       // Item::glyph(), Item::position()
#include "abilities/Ability.h"// Ability::kind(), Ability::cooldown()
#include "core/Enums.h"        // AbilityKind — for HUD label mapping.

// ---------------------------------------------------------------------------
// Anonymous-namespace constants — avoid magic numbers in the implementation.
// ---------------------------------------------------------------------------
namespace {

    /// Width (columns) of the HUD separator line.
    constexpr int HUD_SEPARATOR_WIDTH = 60;

    /// Maximum ray length for the Fire direction-prompt echo.
    /// Used only in console display — not a game-rule constant.
    constexpr int MAX_LABEL_WIDTH = 12;

    /// Number of arrow-key scan codes to recognize on Windows.
    constexpr int ARROW_UP_SCAN    = 72;
    constexpr int ARROW_DOWN_SCAN  = 80;
    constexpr int ARROW_LEFT_SCAN  = 75;
    constexpr int ARROW_RIGHT_SCAN = 77;

    /// Extended-key prefix bytes emitted by _getch() for arrow and function keys.
    constexpr int WIN_EXTENDED_PREFIX_1 = 0;   // Emitted by some keyboards.
    constexpr int WIN_EXTENDED_PREFIX_2 = 224; // 0xE0 — emitted by arrow keys.

    /// ASCII code for the Space bar.
    constexpr int KEY_SPACE = 32;

    /// ASCII code for Ctrl+S (Save shortcut).
    constexpr int KEY_CTRL_S = 19; // 'S' - 64 = 19 in ASCII.

} // anonymous namespace

namespace dga {

// =============================================================================
// Construction / destruction
// =============================================================================

ConsoleRenderer::ConsoleRenderer()
{
    // Nothing to initialize on Windows; POSIX raw-mode is set on first readKey
    // call rather than at construction so the terminal remains normal until
    // input is actually needed.
}

ConsoleRenderer::~ConsoleRenderer()
{
    // On POSIX we restore the terminal to the canonical (line-buffered) mode
    // so the shell prompt is not left in raw-mode after the game exits.
#ifndef _WIN32
    struct termios t;
    if (tcgetattr(STDIN_FILENO, &t) == 0) {
        t.c_lflag |= (ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }
#endif
}

// =============================================================================
// Private helper: clearScreen
// =============================================================================

/// Clears the terminal in a cross-platform way.
/// On Windows: system("cls") achieves a clean redraw without ANSI support.
/// On POSIX:   the ANSI escape "\033[2J\033[H" is faster (no subprocess) and
///             works on virtually every modern POSIX terminal.
void ConsoleRenderer::clearScreen() const
{
#ifdef _WIN32
    system("cls");                          // Windows — standard console clear.
#else
    std::cout << "\033[2J\033[H";           // ANSI — clear + cursor home.
    std::cout.flush();
#endif
}

// =============================================================================
// Private helper: cellGlyph
// =============================================================================

/// Returns the single character to display at grid position (x, y).
/// Priority: Player > Enemy > Item > Tile.
char ConsoleRenderer::cellGlyph(const GameState& state, int x, int y) const
{
    const Vec2 cell(x, y);

    // ---- Highest priority: the player ('@') --------------------------------
    if (state.player().position() == cell) {
        return state.player().glyph(); // always '@'
    }

    // ---- Second priority: enemies (by their own glyph) --------------------
    for (const auto& enemyPtr : state.enemies()) {
        if (enemyPtr && enemyPtr->position() == cell) {
            return enemyPtr->glyph();
        }
    }

    // ---- Third priority: floor items (by their own glyph) -----------------
    for (const auto& itemPtr : state.items()) {
        if (itemPtr && itemPtr->position() == cell) {
            return itemPtr->glyph();
        }
    }

    // ---- Fallback: tile symbol --------------------------------------------
    if (state.map().typeAt(cell) == TileType::Wall) {
        return '#';
    }
    return '.';
}

// =============================================================================
// Private helper: drawHUD
// =============================================================================

/// Prints a multi-line HUD below the map showing all player-facing counters.
void ConsoleRenderer::drawHUD(const GameState& state, const Config& config) const
{
    // Suppress the "config unused" warning — it is kept for future HUD extensions
    // (charge-meter max display, etc.) and matches the interface contract.
    (void)config;

    const Player& p = state.player();

    // ---- Separator line ---------------------------------------------------
    for (int i = 0; i < HUD_SEPARATOR_WIDTH; ++i) {
        std::cout << '-';
    }
    std::cout << '\n';

    // ---- Health | Ammo | Range | Armor | Shield ----------------------------
    std::cout << " HP: " << p.health() << "/" << p.maxHealth()
              << "   Ammo: " << p.ammo()
              << "   Range: " << p.fireRange()
              << "   Armor: " << p.armor()
              << "   Shield: " << (p.isShielded() ? "ON [" + std::to_string(p.shieldRemainingTurns()) + " turns]" : "off")
              << '\n';

    // ---- Wave | Score | Turn | Charge meter --------------------------------
    std::cout << " Wave: " << state.waveNumber()
              << "   Score: " << state.score()
              << "   Turn: " << state.turnCount()
              << "   Charge: " << p.chargeMeter() << "/" << config.chargeMeterMax()
              << '\n';

    // ---- Gold (Feature 1) -------------------------------------------------
    // Gold is the spendable currency awarded by Treasure pickups (in addition
    // to Score). It is shown on its own line so the player can read their
    // wallet at a glance before entering the post-wave Shop.
    std::cout << " Gold: " << state.gold() << '\n';

    // ---- Ability cooldowns ------------------------------------------------
    std::cout << " Abilities: ";
    const auto& abilities = p.abilities();
    if (abilities.empty()) {
        std::cout << "(none)";
    } else {
        for (const auto& abilPtr : abilities) {
            if (!abilPtr) { continue; }

            // Map AbilityKind to a short label for the HUD.
            std::string label;
            switch (abilPtr->kind()) {
                case AbilityKind::Dash:   label = "[1]Dash";   break;
                case AbilityKind::Nova:   label = "[2]Nova";   break;
                case AbilityKind::Shield: label = "[3]Shield"; break;
                case AbilityKind::Blink:  label = "[4]Blink";  break;
                default:                  label = "[?]?";      break;
            }

            if (abilPtr->isReady()) {
                std::cout << label << "(RDY) ";
            } else {
                // Show remaining cooldown turns so the player knows when it is ready.
                std::cout << label << "(" << abilPtr->cooldownRemaining() << ") ";
            }
        }
    }
    std::cout << '\n';

    // ---- Second separator -------------------------------------------------
    for (int i = 0; i < HUD_SEPARATOR_WIDTH; ++i) {
        std::cout << '-';
    }
    std::cout << '\n';
}

// =============================================================================
// Private helper: drawEventLog
// =============================================================================

/// Prints the most recent event log messages beneath the HUD (R29.3).
void ConsoleRenderer::drawEventLog(const EventLog& log, const Config& config) const
{
    // Ask the EventLog for only the capacity the HUD is configured to display.
    const int capacity = config.eventLogDisplayCapacity();
    std::vector<std::string> recent = log.recent(capacity);

    std::cout << " Recent events:\n";
    if (recent.empty()) {
        std::cout << "  (no events yet)\n";
    } else {
        for (const std::string& msg : recent) {
            std::cout << "  " << msg << '\n';
        }
    }
}

// =============================================================================
// Private helper: readKey
// =============================================================================

/// Reads a single raw character from the keyboard.
///
/// On Windows: _getch() returns the character immediately without requiring Enter.
/// On POSIX:   the terminal is temporarily put in raw (non-canonical, no-echo)
///             mode, one character is read, then the terminal is restored.
int ConsoleRenderer::readKey() const
{
#ifdef _WIN32
    // _getch() does not echo and does not require Enter.
    return _getch();
#else
    // POSIX: switch to raw mode, read one byte, restore canonical mode.
    struct termios oldSettings, rawSettings;
    tcgetattr(STDIN_FILENO, &oldSettings);
    rawSettings = oldSettings;
    rawSettings.c_lflag &= ~(ICANON | ECHO); // disable line buffering + echo.
    rawSettings.c_cc[VMIN]  = 1;             // read returns after 1 character.
    rawSettings.c_cc[VTIME] = 0;             // no timeout.
    tcsetattr(STDIN_FILENO, TCSANOW, &rawSettings);

    int ch = std::cin.get();

    tcsetattr(STDIN_FILENO, TCSANOW, &oldSettings);
    return ch;
#endif
}

// =============================================================================
// IRenderer override: drawFrame
// =============================================================================

/// Renders the complete game screen:
///   1. Clears the terminal.
///   2. Draws the GridMap row by row using cellGlyph().
///   3. Draws the HUD bar.
///   4. Draws the event log.
void ConsoleRenderer::drawFrame(const GameState& state,
                                const Config&    config,
                                const EventLog&  log)
{
    clearScreen();

    // ---- Render the dungeon map -------------------------------------------
    const int mapW = state.map().width();
    const int mapH = state.map().height();

    for (int y = 0; y < mapH; ++y) {
        for (int x = 0; x < mapW; ++x) {
            std::cout << cellGlyph(state, x, y);
        }
        std::cout << '\n';
    }

    // ---- HUD bar below the map -------------------------------------------
    drawHUD(state, config);

    // ---- Event log messages (R29.3) --------------------------------------
    drawEventLog(log, config);

    // ---- Controls reminder -----------------------------------------------
    std::cout << " Controls: WASD=Move  F=Fire  1-4=Ability  Space=Wait  Q=Quit  Ctrl+S=Save\n";

    std::cout.flush();
}

// =============================================================================
// IRenderer override: pollInput
// =============================================================================

/// Blocks until the player presses a recognized key and returns an InputCommand.
/// Unrecognized keys are silently discarded; the function loops until a valid
/// key arrives.
InputCommand ConsoleRenderer::pollInput()
{
    // Loop until we get a key we understand.
    for (;;) {
        int ch = readKey();

        // ---- Windows extended-key sequences (arrows, function keys) --------
        // _getch() returns 0x00 or 0xE0 as a prefix before the scan code when
        // the user presses an arrow key or similar.  We read the second byte to
        // determine which key it was.
#ifdef _WIN32
        if (ch == WIN_EXTENDED_PREFIX_1 || ch == WIN_EXTENDED_PREFIX_2) {
            int scanCode = readKey(); // second byte gives the actual key.
            switch (scanCode) {
                case ARROW_UP_SCAN:    return InputCommand(InputCommand::Type::Move, Vec2( 0, -1));
                case ARROW_DOWN_SCAN:  return InputCommand(InputCommand::Type::Move, Vec2( 0, +1));
                case ARROW_LEFT_SCAN:  return InputCommand(InputCommand::Type::Move, Vec2(-1,  0));
                case ARROW_RIGHT_SCAN: return InputCommand(InputCommand::Type::Move, Vec2(+1,  0));
                default: continue; // Unrecognized extended key; discard.
            }
        }
#endif

        // ---- Case-insensitive single-character dispatch --------------------
        // Convert to uppercase so the player can use lowercase or uppercase.
        char c = static_cast<char>(ch);
        if (c >= 'a' && c <= 'z') { c = static_cast<char>(c - 32); }

        switch (c) {
            // ---- Movement (WASD) ------------------------------------------
            case 'W': return InputCommand(InputCommand::Type::Move, Vec2( 0, -1));
            case 'S': return InputCommand(InputCommand::Type::Move, Vec2( 0, +1));
            case 'A': return InputCommand(InputCommand::Type::Move, Vec2(-1,  0));
            case 'D': return InputCommand(InputCommand::Type::Move, Vec2(+1,  0));

            // ---- Fire (F) — ask for a direction with a follow-up prompt ----
            case 'F': {
                // Prompt the player for a firing direction.
                std::cout << "\n Fire direction (WASD): ";
                std::cout.flush();

                // Read a second key for the firing direction.
                int dirCh = readKey();
                char dc = static_cast<char>(dirCh);
                if (dc >= 'a' && dc <= 'z') { dc = static_cast<char>(dc - 32); }

                Vec2 fireDir(0, 0);
                switch (dc) {
                    case 'W': fireDir = Vec2( 0, -1); break;
                    case 'S': fireDir = Vec2( 0, +1); break;
                    case 'A': fireDir = Vec2(-1,  0); break;
                    case 'D': fireDir = Vec2(+1,  0); break;
                    default:  break; // Invalid direction; fire {0,0} (combat rejects).
                }
                return InputCommand(InputCommand::Type::Fire, fireDir);
            }

            // ---- Abilities (1/2/3/4) → indices 0/1/2/3 -------------------
            case '1': return InputCommand(InputCommand::Type::UseAbility, Vec2(0, 0), 0);
            case '2': return InputCommand(InputCommand::Type::UseAbility, Vec2(0, 0), 1);
            case '3': return InputCommand(InputCommand::Type::UseAbility, Vec2(0, 0), 2);
            case '4': return InputCommand(InputCommand::Type::UseAbility, Vec2(0, 0), 3);

            // ---- Quit (Q) -------------------------------------------------
            case 'Q': return InputCommand(InputCommand::Type::Quit);

            // ---- Wait (Space / Enter) ------------------------------------
            case ' ':
            // Fall through: Enter key (ASCII 13 / 10).
                return InputCommand(InputCommand::Type::Wait);

            default:
                // Ctrl+S produces ASCII 19 (decimal) — Save.
                if (ch == KEY_CTRL_S) {
                    return InputCommand(InputCommand::Type::Save);
                }
                // Newline / carriage return also treated as Wait.
                if (ch == '\n' || ch == '\r') {
                    return InputCommand(InputCommand::Type::Wait);
                }
                // Anything else is unrecognized — loop and ask again.
                continue;
        }
    }
}

// =============================================================================
// IRenderer override: drawMenu
// =============================================================================

/// Prints a selection menu; the entry at `selectedIndex` is prefixed "> ".
void ConsoleRenderer::drawMenu(const std::vector<std::string>& options,
                               int selectedIndex)
{
    std::cout << '\n';
    for (int i = 0; i < static_cast<int>(options.size()); ++i) {
        if (i == selectedIndex) {
            std::cout << " > " << options[i] << '\n';
        } else {
            std::cout << "   " << options[i] << '\n';
        }
    }
    std::cout << '\n';
    std::cout.flush();
}

// =============================================================================
// IRenderer override: drawMessage
// =============================================================================

/// Prints `message` followed by a newline.
void ConsoleRenderer::drawMessage(const std::string& message)
{
    std::cout << "\n " << message << '\n';
    std::cout.flush();
}

} // namespace dga
