// =============================================================================
// io/ScoreBoard.cpp
//
// Purpose:
//   Definitions for ScoreBoard declared in io/ScoreBoard.h.
//
//   Responsibilities:
//     * computeScore   — apply the wave*100 + kills*10 + treasure*1 formula
//                        (R24.1) using named local constants.
//     * append         — write one ScoreEntry to the end of the file (R25.1).
//     * loadTop        — read all entries, sort with insertion sort (R25.2),
//                        return the top N (R25.4). Missing file → empty (R3.3);
//                        malformed line → skip + EventLog message (R3.4).
//     * insertionSortDescending — the in-house sort (R25.2, R25.3).
//
// File format (one entry per line):
//   <playerName> <score> <wave> <kills>
//   Example:  "Alice 3450 12 28"
//
// Note: playerName must not contain whitespace (it is read as a single word
// by std::cin >> / ifstream >>).  If names with spaces are needed in the
// future, the format can be changed to CSV; the current spec does not require
// space-in-name support.
// =============================================================================
#include "io/ScoreBoard.h"

#include <algorithm> // std::min — cap returned vector to requested count.
#include <fstream>   // std::ifstream, std::ofstream — file read/write.
#include <sstream>   // std::istringstream — parse each line independently.
#include <string>    // std::string, std::getline.

#include "systems/EventLog.h" // EventLog::append — log malformed lines (R3.4).

namespace dga {

// ---------------------------------------------------------------------------
// Scoring weight constants (R24.1, R8.5).
//
// These match Config::scoreWeightWave/Kill/Treasure (100 / 10 / 1) and are
// defined here as named constants so the formula has zero magic numbers.
// ---------------------------------------------------------------------------
static constexpr int WAVE_WEIGHT     = 100; ///< Points awarded per wave reached.
static constexpr int KILL_WEIGHT     = 10;  ///< Points awarded per enemy killed.
static constexpr int TREASURE_WEIGHT = 1;   ///< Points awarded per treasure unit.

// ---------------------------------------------------------------------------
// ScoreBoard::computeScore
// ---------------------------------------------------------------------------

int ScoreBoard::computeScore(int waveNumber, int killCount, int treasure) {
    // Formula (R24.1): wave * 100 + kills * 10 + treasure * 1.
    // Named constants make this self-documenting with no magic numbers (R8.5).
    return (waveNumber * WAVE_WEIGHT)
         + (killCount  * KILL_WEIGHT)
         + (treasure   * TREASURE_WEIGHT);
}

// ---------------------------------------------------------------------------
// ScoreBoard::append
// ---------------------------------------------------------------------------

void ScoreBoard::append(const ScoreEntry& entry, const std::string& filePath) {
    // Open in append mode so existing records are never lost (R25.1).
    // std::ios::app positions the write head at the end of the file and
    // creates the file if it does not yet exist.
    std::ofstream outFile(filePath, std::ios::app);

    // If the file cannot be opened (e.g. directory does not exist), silently
    // return — the game must not crash because a score could not be saved
    // (R3.3: "treat data as empty and continue without terminating").
    if (!outFile.is_open()) {
        return;
    }

    // Write the record as four space-separated fields on one line.
    // Format: <playerName> <score> <wave> <kills>
    outFile << entry.playerName
            << ' ' << entry.score
            << ' ' << entry.wave
            << ' ' << entry.kills
            << '\n';

    // outFile destructor flushes and closes on scope exit (RAII).
}

// ---------------------------------------------------------------------------
// ScoreBoard::insertionSortDescending  (R25.2, R25.3)
// ---------------------------------------------------------------------------

void ScoreBoard::insertionSortDescending(std::vector<ScoreEntry>& entries) {
    // Classic insertion sort; the comparison is GREATER-THAN so the result is
    // descending (highest score at index 0).  std::sort is deliberately NOT
    // used (R25.2, R25.3).
    //
    // Complexity: O(n^2) worst case, O(n) when already sorted.  For typical
    // leaderboard sizes (a few dozen to a few hundred entries) this is fast
    // enough and far cleaner than more complex algorithms.
    const int totalEntries = static_cast<int>(entries.size());

    for (int i = 1; i < totalEntries; ++i) {
        // The entry being inserted into the sorted prefix entries[0..i-1].
        ScoreEntry currentEntry = entries[i];

        // Shift every smaller entry one position to the right to make room.
        int j = i - 1;
        while (j >= 0 && entries[j].score < currentEntry.score) {
            entries[j + 1] = entries[j];
            --j;
        }

        // Place the current entry in its sorted position.
        entries[j + 1] = currentEntry;
    }
}

// ---------------------------------------------------------------------------
// ScoreBoard::loadTop
// ---------------------------------------------------------------------------

std::vector<ScoreEntry> ScoreBoard::loadTop(int                count,
                                             const std::string&  filePath,
                                             EventLog&           log) {
    std::vector<ScoreEntry> allEntries;

    // ---- Step 1: Open the file (R3.3 / R25.4) -----------------------------
    // If the file does not exist, return an empty vector and do NOT log an
    // error: an absent file simply means no scores have been saved yet.
    std::ifstream inFile(filePath);
    if (!inFile.is_open()) {
        // R3.3: "treat the data as empty and continue without terminating."
        // R25.4: "display an empty leaderboard without error."
        return allEntries; // empty
    }

    // ---- Step 2: Parse each line (R3.4) -----------------------------------
    std::string currentLine;
    while (std::getline(inFile, currentLine)) {
        // Skip completely blank lines without logging a warning.
        if (currentLine.empty()) {
            continue;
        }

        std::istringstream lineStream(currentLine);
        ScoreEntry parsedEntry;

        // Expect exactly four fields: name, score, wave, kills.
        if (!(lineStream >> parsedEntry.playerName
                         >> parsedEntry.score
                         >> parsedEntry.wave
                         >> parsedEntry.kills)) {
            // R3.4: malformed line → skip with a descriptive EventLog message.
            log.append("ScoreBoard: skipping malformed line: [" + currentLine + "]");
            continue;
        }

        allEntries.push_back(parsedEntry);
    }

    // ---- Step 3: Sort descending by score (R25.2, R25.3) -----------------
    // Use the in-house insertion sort; std::sort is NOT called (R25.2, R25.3).
    insertionSortDescending(allEntries);

    // ---- Step 4: Return the top `count` entries (R25.4) ------------------
    // Cap with std::min to avoid returning more than what was requested or
    // more than the vector actually holds.
    const int returnCount = std::min(count, static_cast<int>(allEntries.size()));
    return std::vector<ScoreEntry>(allEntries.begin(),
                                   allEntries.begin() + returnCount);
}

} // namespace dga
