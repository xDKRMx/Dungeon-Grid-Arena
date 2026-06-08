// =============================================================================
// render/ConsoleRenderer.h
//
// Purpose:
//   ConsoleRenderer is the ASCII terminal implementation of IRenderer (R4.5,
//   R28.3). It draws the game entirely with std::cout / std::cerr (no external
//   graphics library) and reads input with _getch() on Windows (or std::cin on
//   other platforms) so the build has zero external dependencies (R28.5).
//
//   This is the guaranteed-to-run baseline renderer: it is what the automated
//   tests and the mandatory screen-recording use, and it is the fallback whenever
//   a graphical renderer is unavailable or disabled (BUILD_WITH_RAYLIB=OFF).
//
// drawFrame layout:
//   The screen is cleared (system("cls") on Windows / ANSI escape on others),
//   then the GridMap is printed row-by-row with the following symbol mapping:
//     '#' — Wall tile
//     '.' — Floor tile (empty)
//     '@' — Player
//     glyph() — any Enemy or Item on that cell
//   Below the map a single HUD line is printed showing:
//     HP | Ammo | Shield | Wave | Score | Turn | Ability cooldowns
//   Finally the most recent Event_Log messages are printed (R29.3).
//
// pollInput key mapping:
//   W / Up    → Move {0,-1}     A / Left  → Move {-1, 0}
//   S / Down  → Move {0,+1}     D / Right → Move {+1, 0}
//   F         → Fire (prompts a second keypress for direction)
//   1/2/3/4   → UseAbility 0/1/2/3
//   Q         → Quit
//   Space     → Wait
//   Ctrl+S    → Save
//   Arrow keys also work on platforms where _getch() returns them as two-byte
//   sequences (0x00/0xE0 prefix + code).
//
// Requirements: 28.3, 28.5, 28.6, 29.1, 29.3, 4.5
//
// Why a .h/.cpp split:
//   ConsoleRenderer has real logic (drawing, input decoding); declarations live
//   here and definitions live in ConsoleRenderer.cpp (R2.1).
//
// Layer: render (depends on render/IRenderer.h; no other logic-layer headers
//   are needed because all the types arrive through IRenderer.h already).
// =============================================================================
#pragma once

#include "render/IRenderer.h" // IRenderer and the types its methods use.

namespace dga {

// =============================================================================
// ConsoleRenderer — ASCII terminal renderer (R4.5, R28.3)
// =============================================================================

/// Renders the game as ASCII art to the terminal and reads input from the
/// keyboard (R28.3, R28.5).
///
/// Derives from IRenderer and overrides all four pure virtual methods:
///   drawFrame()   — clear screen + draw map + HUD + event log.
///   pollInput()   — read one keypress and convert to InputCommand.
///   drawMenu()    — print menu options with "> " prefix on selected entry.
///   drawMessage() — print the message with std::cout.
///
/// No external libraries are used; the only platform-specific code is the
/// screen-clear (system("cls") on Windows, ANSI escape on POSIX) and the
/// single-character read (_getch on Windows, raw-terminal on POSIX) (R28.5).
class ConsoleRenderer : public IRenderer {
public:
    // ---- Construction / destruction ---------------------------------------

    /// Construct the console renderer.
    ///
    /// No initialization is needed (the console already exists when the
    /// process starts).  The constructor is still declared so the header
    /// is self-documenting.
    ConsoleRenderer();

    /// Destructor.
    ///
    /// Restores the terminal to its original mode on POSIX (where raw input
    /// mode was set) and does nothing additional on Windows.
    ~ConsoleRenderer() override;

    // ---- IRenderer interface overrides ------------------------------------

    /// Clear the screen and render one complete game frame (R28.3, R29.1,
    /// R29.3).
    ///
    /// Drawing order:
    ///   1. Clear screen (cross-platform).
    ///   2. Print column indices for debug reference (top ruler).
    ///   3. For each row of the GridMap, print each cell:
    ///        '#' Wall, '.' empty Floor, '@' Player, enemy/item glyph otherwise.
    ///   4. Print a blank line separator.
    ///   5. Print the HUD bar: HP, ammo, shield status, wave, score, turn, and
    ///      all ability cooldowns.
    ///   6. Print a blank line separator.
    ///   7. Print the most-recent event log messages (R29.3).
    ///
    /// @param state  the authoritative run state (read-only).
    /// @param config the balancing configuration (read-only).
    /// @param log    the event log (read-only).
    void drawFrame(const GameState& state,
                   const Config&    config,
                   const EventLog&  log) override;

    /// Block until the player presses a recognized key (R28.6).
    ///
    /// On Windows uses _getch() (no Enter key required); on other platforms
    /// uses std::cin.get() in raw-terminal mode. Unrecognized keys are
    /// silently ignored; the function loops until a valid key arrives.
    ///
    /// Returns an InputCommand whose `type` and `direction`/`abilityIndex`
    /// fields encode the player's intent (see the header file block comment
    /// for the complete key → command mapping).
    ///
    /// @return the InputCommand corresponding to the player's keypress.
    InputCommand pollInput() override;

    /// Print a vertical selection menu (R28.2).
    ///
    /// Each option is printed on its own line.  The entry at `selectedIndex`
    /// is prefixed with "> "; all others are prefixed with "  ".
    ///
    /// @param options       the menu items to display.
    /// @param selectedIndex the 0-based index of the highlighted item.
    void drawMenu(const std::vector<std::string>& options,
                  int selectedIndex) override;

    /// Print `message` to stdout followed by a newline (R28.2).
    ///
    /// Full-screen messages are displayed inline in the terminal scroll buffer.
    /// A trailing newline is always appended so subsequent output is not
    /// concatenated onto the same line.
    ///
    /// @param message the text to display; embedded '\n' characters are honoured
    ///                by std::cout automatically.
    void drawMessage(const std::string& message) override;

private:
    // ---- Internal helper functions ----------------------------------------

    /// Clear the terminal screen in a cross-platform way.
    ///
    /// On Windows: calls system("cls").
    /// On other platforms: writes the ANSI escape sequence "\033[2J\033[H"
    /// which moves the cursor to the top-left and clears the screen.
    void clearScreen() const;

    /// Determine the display character for a single map cell at (x, y).
    ///
    /// Priority order (highest first):
    ///   1. Player occupies this cell → '@'
    ///   2. Any living enemy occupies this cell → enemy.glyph()
    ///   3. Any item lies on this cell → item.glyph()
    ///   4. The tile type → '#' for Wall, '.' for Floor
    ///
    /// @param state the run state supplying player position, enemies, items.
    /// @param x     column index of the cell.
    /// @param y     row index of the cell.
    /// @return the single character to print for this cell.
    char cellGlyph(const GameState& state, int x, int y) const;

    /// Print the HUD status bar below the map.
    ///
    /// Format (single line):
    ///   HP:<hp>/<maxHp>  Ammo:<ammo>  Shield:<On/Off>
    ///   Wave:<n>  Score:<n>  Turn:<n>
    ///   Abilities: [<kind>:<cd>] ...
    ///
    /// @param state  the run state (player, counters).
    /// @param config the balancing configuration (unused currently but kept
    ///               for future HUD extensions such as a charge-meter max).
    void drawHUD(const GameState& state, const Config& config) const;

    /// Print the most-recent event log entries below the HUD.
    ///
    /// Asks the EventLog for the last eventLogDisplayCapacity() entries and
    /// prints each on its own line (R29.3).
    ///
    /// @param log    the event log to read from.
    /// @param config the balancing configuration — provides the display
    ///               capacity (eventLogDisplayCapacity()).
    void drawEventLog(const EventLog& log, const Config& config) const;

    /// Read a single raw keypress character.
    ///
    /// Abstracted into a helper so the multi-byte arrow-key sequences (Windows
    /// _getch returns 0x00 or 0xE0 followed by a scan code) can be handled
    /// in one place rather than duplicating the logic in pollInput().
    ///
    /// @return the raw character/scan code of the next keystroke.
    int readKey() const;
};

} // namespace dga
