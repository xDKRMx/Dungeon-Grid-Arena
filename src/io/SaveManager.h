// =============================================================================
// io/SaveManager.h
//
// Purpose:
//   SaveManager serializes and restores a complete run state to/from a tagged
//   text file (R3.2, R26.1-R26.4), enabling save-and-resume gameplay.
//
// Tagged text format (one tag-per-line):
//
//   Scalar run state
//   ----------------
//   SEED      <uint>          — RNG seed (used to re-seed on load, R26.4)
//   WAVE      <int>           — current Wave_Number
//   SCORE     <int>           — accumulated Score
//   TURN      <int>           — Turn_Count elapsed
//   KILLS     <int>           — enemies killed this run
//   CHARGE    <int>           — Charge_Meter value
//
//   Player state
//   ------------
//   PLAYER_POS    <x> <y>     — grid position
//   PLAYER_HEALTH <int>       — current health
//   PLAYER_AMMO   <int>       — ammo count
//   PLAYER_ARMOR  <int>       — armor (damage reduction)
//   PLAYER_ATTACK <int>       — base attack value
//
//   Map dimensions and tiles
//   ------------------------
//   MAP_WIDTH  <int>
//   MAP_HEIGHT <int>
//   TILE <x> <y> <W|F>        — one line per tile; W = Wall, F = Floor
//
//   Enemies (re-created by kind)
//   ----------------------------
//   ENEMY <kindTag> <x> <y> <health>
//
//   Items (re-created by kind, with kind-specific extra fields)
//   -----------------------------------------------------------
//   ITEM <kindTag> <x> <y> [extra...]
//     kindTag      extra fields
//     -----------  -------------------------------------------
//     HealthPotion (none)
//     Weapon       <attackValue> <ranged:0|1>
//     AmmoItem     <amount>
//     Armor        <armorBonus>
//     Treasure     <value>
//
// Enemy kind tags (matching EntityKind enumerator names):
//   Melee, Rook, Bishop, Queen, Fast, Boss
//
// Item kind tags (matching ItemKind enumerator names):
//   HealthPotion, Weapon, AmmoItem, Armor, Treasure
//
// Guarantees (R26.3, R3.3, R3.4):
//   * load() performs a transactional read: it fills a staging area and only
//     commits to `state` when every line has been successfully parsed. If any
//     part of the file is missing or malformed the state is LEFT UNCHANGED
//     (no partial overwrite), an EventLog message is appended, and the function
//     returns false.
//   * Missing file → report "no save file found" via EventLog, return false.
//
// Layer: io (depends on systems/GameState, systems/EventLog,
//   entities/Enemy, items/*, world/GridMap, core — never on render).
// =============================================================================
#pragma once

#include <string> // std::string — file path parameter.

namespace dga {

class EventLog;  // Forward decl: both save() and load() append messages here.
class GameState; // Forward decl: the run state to serialize / restore.

/// Serializes and deserializes a full run state (R3.2, R26.1-R26.4).
///
/// Both methods are static because SaveManager carries no per-instance state:
/// it is a thin I/O service that operates entirely through its parameters.
class SaveManager {
public:
    // ---- Save (R26.1) -------------------------------------------------------

    /// Write the current run state to a tagged text file (R26.1).
    ///
    /// Serializes all state required for a complete run restoration:
    ///   - RNG seed, Wave_Number, Score, Turn_Count, enemies killed (R26.1)
    ///   - Charge_Meter
    ///   - Player position, Health, ammo, armor, attack
    ///   - GridMap dimensions and every tile type (R26.1)
    ///   - Every live enemy (kind, position, current Health) (R26.1)
    ///   - Every floor item (kind, position, kind-specific fields) (R26.1)
    ///
    /// On success appends an informational message to `log`.
    /// On failure (e.g. cannot open file) appends an error message and returns
    /// without crashing (R3.3 — game must continue without terminating).
    ///
    /// @param state    the run state to serialize (read-only).
    /// @param filePath path to the save file to write (overwritten if exists).
    /// @param log      the EventLog to receive success/error messages.
    /// @return true when every byte was flushed to disk successfully; false
    ///         when the file could not be opened or a write error occurred.
    ///         Callers (Game.cpp) use the return value to show the player a
    ///         "Saved." vs. "Save FAILED" message instead of falsely claiming
    ///         success on a silent I/O error.
    static bool save(const GameState& state,
                     const std::string& filePath,
                     EventLog& log);

    // ---- Load (R26.2, R26.3, R26.4) ----------------------------------------

    /// Read a save file and restore the run state (R26.2).
    ///
    /// Algorithm:
    ///   1. Open the file; on failure log "no save file" and return false (R3.3,
    ///      R26.3).
    ///   2. Parse the file into a staging GameState.  If any line is malformed
    ///      or a required tag is missing, log a descriptive message and return
    ///      false WITHOUT modifying `state` (R3.4, R26.3 — no partial overwrite).
    ///   3. Re-seed `state.rng()` with the saved SEED so the run continues
    ///      deterministically from the saved point (R26.4).
    ///   4. Restore player stats, map tiles, enemies, and items into `state`
    ///      (R26.2).  The existing enemies and items collections are CLEARED
    ///      before re-population so there are no leftovers from a prior session.
    ///   5. Append a success message and return true (R26.2).
    ///
    /// @param filePath path to the save file to read.
    /// @param state    the run state to restore INTO (modified only on success).
    /// @param log      the EventLog to receive success/error messages (R3.4).
    /// @return true when the state has been fully restored; false otherwise.
    static bool load(const std::string& filePath,
                     GameState& state,
                     EventLog& log);
};

} // namespace dga
