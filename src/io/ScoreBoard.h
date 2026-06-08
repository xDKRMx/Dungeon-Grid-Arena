// =============================================================================
// io/ScoreBoard.h
//
// Purpose:
//   ScoreBoard provides three capabilities needed for the high-score leaderboard
//   (R3.1, R24.1, R25.1-R25.4):
//
//     1.  computeScore  — pure function; computes a run's Score from wave,
//                         kills, and treasure using the formula
//                         wave*100 + kills*10 + treasure*1  (R24.1).
//                         Named local constants (no magic numbers, R8.5).
//
//     2.  append        — File I/O; appends a single ScoreEntry to the text
//                         file (one record per line, space-delimited) (R3.1,
//                         R25.1).
//
//     3.  loadTop       — File I/O; reads all records, sorts them with an
//                         in-house insertion sort (NOT std::sort — R25.2/R25.3),
//                         and returns the top `count` entries in descending
//                         score order. Missing file → empty vector (R3.3).
//                         Malformed lines are silently skipped with an
//                         EventLog message (R3.4).
//
// File format (one entry per line):
//   <playerName> <score> <wave> <kills>
//
// Why a static free-function style:
//   ScoreBoard has no per-instance state; every operation is either a pure
//   computation or a thin wrapper around file I/O that takes its path
//   explicitly. Making them static member functions keeps the class coherent
//   while avoiding the need for a separate "global services" pattern.
//
// Why insertion sort (R25.2, R25.3):
//   The requirement explicitly calls for an in-house sorting algorithm.
//   Insertion sort is used because it is simple to implement, readable, and
//   perfectly adequate for the small leaderboard lists this function handles
//   (at most a few hundred entries).  std::sort is deliberately NOT used.
//
// Layer: io (depends on systems/EventLog, core — never on render).
// =============================================================================
#pragma once

#include <string>  // std::string — player name and file path.
#include <vector>  // std::vector — loadTop return type.

namespace dga {

class EventLog; // Forward declaration: loadTop reports malformed lines here.

// ---------------------------------------------------------------------------
// ScoreEntry — POD-style record for one high-score row.
// ---------------------------------------------------------------------------

/// One row in the high-score leaderboard (R25.1).
///
/// All four fields are written to and read from the persistent file so the
/// leaderboard can display the full run summary (name, score, wave reached,
/// and total kills), not merely the headline score.
struct ScoreEntry {
    std::string playerName; ///< The player's chosen display name.
    int         score;      ///< The final computed Score for this run (R24.1).
    int         wave;       ///< The highest Wave_Number the player reached.
    int         kills;      ///< The total number of enemies killed in the run.
};

// ---------------------------------------------------------------------------
// ScoreBoard — static leaderboard utilities.
// ---------------------------------------------------------------------------

/// Static utilities for scoring, persisting, and ranking leaderboard entries.
///
/// Every method is `static` because ScoreBoard carries no instance state; it
/// is a namespace-like collection of related free operations grouped under one
/// class name for encapsulation (R1.1).
class ScoreBoard {
public:
    // --- Scoring formula (R24.1) -------------------------------------------

    /// Compute a run's final Score from the three contributing values (R24.1).
    ///
    /// Formula: (waveNumber * WAVE_WEIGHT)
    ///        + (killCount  * KILL_WEIGHT)
    ///        + (treasure   * TREASURE_WEIGHT)
    ///
    /// The three weights are named local constants defined in the .cpp (100,
    /// 10, and 1 respectively), matching Config's scoreWeight* accessors and
    /// the formula specified in R24.1. They are NOT literals at the call site.
    ///
    /// @param waveNumber   the highest wave the player reached (>= 1).
    /// @param killCount    total enemies killed in the run (>= 0).
    /// @param treasure     total raw treasure value collected (>= 0).
    /// @return the non-negative integer Score.
    static int computeScore(int waveNumber, int killCount, int treasure);

    // --- File persistence (R3.1, R25.1) ------------------------------------

    /// Append one completed run's record to the high-score file (R25.1).
    ///
    /// Opens the file in append mode (creating it if it does not yet exist)
    /// and writes one line in the format:
    ///   <playerName> <score> <wave> <kills>\n
    ///
    /// If the file cannot be opened or written, the failure is silently
    /// swallowed: persisting a score must never crash the game (R3.3/R3.4
    /// "continue without terminating").
    ///
    /// @param entry    the completed run's data to persist.
    /// @param filePath filesystem path to the high-score file (relative or
    ///                  absolute).
    static void append(const ScoreEntry& entry, const std::string& filePath);

    /// Load the all-time leaderboard and return the top entries (R25.1-R25.4).
    ///
    /// Algorithm:
    ///   1. Open `filePath` for reading. If the file does not exist, or cannot
    ///      be opened, return an empty vector without logging (R3.3 / R25.4).
    ///   2. Parse each line into a ScoreEntry. Lines that do not match the
    ///      four-field format are skipped; each skip appends one message to
    ///      `log` naming the offending line (R3.4).
    ///   3. Sort all valid entries in DESCENDING order by `score` using an
    ///      in-house insertion sort (R25.2, R25.3).  std::sort is NOT used.
    ///   4. Return at most `count` entries from the sorted front.
    ///
    /// @param count    how many top entries to return (pass e.g. 10 for the
    ///                  top-10 leaderboard); if count >= total entries, all
    ///                  are returned.
    /// @param filePath filesystem path to the high-score file.
    /// @param log      the EventLog to receive skipped-line warnings (R3.4).
    /// @return a vector of at most `count` ScoreEntry values, sorted by score
    ///         descending (highest score first).
    static std::vector<ScoreEntry> loadTop(int                count,
                                           const std::string&  filePath,
                                           EventLog&           log);

private:
    // -----------------------------------------------------------------------
    // In-house insertion sort (R25.2, R25.3).
    // -----------------------------------------------------------------------

    /// Sort `entries` in descending order by score using insertion sort.
    ///
    /// Insertion sort is chosen because it is easy to audit (demonstrating the
    /// "explicit sorting algorithm" criterion, R25.2), correct for the small
    /// leaderboard sizes expected in practice, and avoids std::sort (R25.3).
    ///
    /// Runs in O(n²) worst case, O(n) best case (already sorted). For n <= a
    /// few hundred entries, this is negligible. The sort is STABLE for equal
    /// scores (insertion sort's natural property).
    ///
    /// @param entries the vector to sort in place (modified).
    static void insertionSortDescending(std::vector<ScoreEntry>& entries);
};

} // namespace dga
