// =============================================================================
// systems/WaveManager.h
//
// Purpose:
//   WaveManager is the stateless service responsible for starting and advancing
//   enemy waves (R17, R18). It spawns enemies at the beginning of each wave,
//   detects when a wave has been cleared, and advances the wave number when the
//   player clears the current one.
//
//   Responsibilities:
//     * startWave() — generate the map, pick spawn tiles via MapGenerator, then
//                     spawn a BossEnemy when waveNumber % 5 == 0 (R18.1) and a
//                     mix of Melee/Rook/Bishop/Queen/Fast enemies otherwise,
//                     scaled with the current wave number (R17.1, R17.3).
//     * isWaveCleared() — returns true when the enemy list is empty (R17.2,
//                         R18.5).
//     * advance() — increments waveNumber in GameState and calls startWave for
//                   the next wave (R17.2, R17.4).
//
//   Enemy mix scaling (R17.3):
//     The number of enemies spawned grows with the wave number. Early waves get
//     mostly melee enemies; later waves introduce ranged types (Rook, Bishop,
//     Queen) and Fast enemies according to a weighted table that shifts as
//     waveNumber increases.
//
// Why a .h/.cpp split:
//   WaveManager contains real logic; declarations live here and definitions in
//   WaveManager.cpp (R2.1).
//
// Layer: systems (depends on entities, world, core).
// =============================================================================
#pragma once

namespace dga {

class Config;
class EventLog;
class GameState;

/// Stateless service that spawns, tracks, and advances enemy waves (R17, R18).
///
/// All member functions are const (no object state). The wave-number is stored
/// in GameState, so WaveManager reads and writes it there rather than carrying
/// its own copy.
class WaveManager {
public:
    /// Default constructor; the class holds no data members.
    WaveManager() = default;

    // ---- Wave start (R17.1, R17.3, R18.1) ----------------------------------

    /// Generate the map for the current wave, spawn enemies, and place them in
    /// the GameState (R17.1, R18.1).
    ///
    /// Map generation:
    ///   Calls MapGenerator::generate to carve a fresh dungeon, then
    ///   MapGenerator::pickSpawns to choose distinct floor tiles for the player
    ///   and each enemy (R9.4). The player is placed on playerStart; enemies are
    ///   placed on the returned enemy spawn tiles.
    ///
    /// Enemy spawning (R17.3, R18.1):
    ///   * If state.waveNumber() % 5 == 0 → spawn ONE BossEnemy (R18.1).
    ///   * Otherwise → spawn a mix of enemy types scaled with wave number; the
    ///     exact count is computed as (base + floor(waveNumber * growthFactor))
    ///     where base and growthFactor are fixed internal constants (no magic
    ///     numbers exposed externally).
    ///
    /// @param state          the authoritative game state; map and enemies are
    ///                       modified in place.
    /// @param config         balancing constants (enemy stats, map dimensions).
    /// @param enemySpawnCount desired number of enemy spawns; the map may reduce
    ///                       this if there are not enough floor tiles (R9.6).
    void startWave(GameState& state,
                   const Config& config,
                   int enemySpawnCount) const;

    // ---- Wave clearance detection (R17.2, R18.5) ---------------------------

    /// Check whether all enemies in the current wave have been defeated.
    ///
    /// A wave is cleared exactly when the enemy list is empty: every enemy,
    /// including minions summoned by a BossEnemy, must be dead before the wave
    /// ends (R18.5).
    ///
    /// @param state the game state to inspect.
    /// @return true when state.enemies() is empty (R17.2, R18.5).
    bool isWaveCleared(const GameState& state) const;

    // ---- Wave advancement (R17.2, R17.4) -----------------------------------

    /// Increment the wave number and begin the next wave (R17.2, R17.4).
    ///
    /// Increments state.waveNumber() by 1 via setWaveNumber, then calls
    /// startWave with a spawn count derived from the new wave number. The
    /// wave count grows monotonically so difficulty only ever increases (R17.3).
    ///
    /// @param state  the game state to advance.
    /// @param config balancing constants for the new wave.
    void advance(GameState& state, const Config& config) const;
};

} // namespace dga
