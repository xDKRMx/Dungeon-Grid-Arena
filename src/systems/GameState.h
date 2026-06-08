// =============================================================================
// systems/GameState.h
//
// Purpose:
//   GameState is the single authoritative container for everything that makes
//   up one in-progress run (R8.3): the dungeon GridMap, the Player, the live
//   Enemies, the Items lying on the floor, and the run counters (Wave_Number,
//   Score, Turn_Count, enemies killed) plus the deterministic Rng. The Game
//   orchestrator owns ONE GameState and hands it by reference to the systems
//   that operate on it, so there are no mutable globals anywhere (R8.3, R1.1).
//
//   GameState is deliberately a STATE container, not a rules engine: it stores
//   and exposes data and provides small mutators, but it contains no game logic
//   (no turn order, no combat math, no spawning rules). Those belong to the
//   systems layer (TurnManager, CombatSystem, WaveManager, ...). Renderers and
//   tests read it through const accessors; systems mutate it through the
//   provided mutators.
//
// The incomplete-type knot (resolved with a forward declaration):
//   GameState owns its enemies as `std::vector<std::unique_ptr<Enemy>>`, but
//   Enemy is implemented later (task 5). A unique_ptr member to an INCOMPLETE
//   type is legal as long as the operations that need the full type (notably
//   the destructor, which must know how to delete an Enemy) are not generated
//   in this header. We therefore only forward-declare Enemy here and DECLARE a
//   destructor; the destructor is DEFINED out-of-line in GameState.cpp, which
//   (once Enemy is complete) will include the full Enemy header. Until then the
//   .cpp can still compile the defaulted destructor because, at this stage,
//   GameState holds no enemies to destroy.
//
// Why a .h/.cpp split:
//   GameState owns real state and an out-of-line destructor, so its
//   declarations live here and its definitions live in GameState.cpp (R2.1).
//
// Layer: systems (depends on world/GridMap, entities/Player, items/Item,
//   core/Rng, core/Config; references Enemy by forward declaration only).
// =============================================================================
#pragma once

#include <memory> // std::unique_ptr - owns enemies and items polymorphically.
#include <vector> // std::vector     - the enemy and item collections.

#include "core/Rng.h"        // Rng    - the run's deterministic randomness (by value).
#include "entities/Player.h" // Player - the hero (held by value).
#include "items/Item.h"      // Item   - owned polymorphically as unique_ptr<Item>.
#include "world/GridMap.h"   // GridMap - the dungeon grid (held by value).

namespace dga {

class Config; // Forward decl: the ctor reads starting values from Config.
class Enemy;  // Forward decl: enemies_ owns Enemy subtypes (implemented in task 5).

/// The owned, authoritative state of a single run (R8.3).
///
/// Holds the map, the hero, the live enemies, the floor items, the run
/// counters, and the deterministic Rng. All members are private and reached
/// through accessors/mutators (R1.2); this class stores state and provides
/// access to it, but applies no game rules itself.
class GameState {
public:
    /// Build the initial run state from configuration and a seed.
    /// @param config the balancing configuration; used to size the GridMap and
    ///        to build the Player's starting stats. Not stored (only read here).
    /// @param seed   the value used to seed the run's Rng, making the whole run
    ///        reproducible (same seed -> same map/spawns/draws) (R26.4).
    /// The Player is built at a placeholder start cell; the real spawn placement
    /// is wired later by the map-generation/wave systems. Wave_Number starts at
    /// 1 and the other counters (Score, Turn_Count, enemies killed) start at 0.
    GameState(const Config& config, unsigned int seed);

    /// Destructor declared here and DEFINED in GameState.cpp so that destroying
    /// the `unique_ptr<Enemy>` members happens in a translation unit where Enemy
    /// is a complete type. Declaring it (rather than letting the compiler emit
    /// an implicit one in this header) is what makes owning a forward-declared
    /// Enemy by unique_ptr legal here (see the file header).
    ~GameState();

    /// Reset the run state back to its starting configuration without
    /// destroying or re-constructing the GameState object itself. Rebuilds
    /// the dungeon map from Config, clears every enemy / item, calls
    /// `Player::reset` to restore the hero, zeroes every counter, and reseeds
    /// the run's random source from `seed`. Used by Game::resetRun when the
    /// player picks "New Game" so the existing GameState instance is re-used
    /// in place.
    /// @param config the balancing configuration to read fresh starting
    ///        values from.
    /// @param seed   the new run's RNG seed (R26.4).
    void reset(const Config& config, unsigned int seed);

    // ---- Map -------------------------------------------------------------

    /// @return a read-only reference to the dungeon map (for renderers/tests).
    const GridMap& map() const;

    /// @return a modifiable reference to the dungeon map (for the generator).
    GridMap& map();

    // ---- Player ----------------------------------------------------------

    /// @return a read-only reference to the hero (for the HUD/tests).
    const Player& player() const;

    /// @return a modifiable reference to the hero (for movement/pickups/combat).
    Player& player();

    // ---- Enemies (owned) -------------------------------------------------

    /// @return a read-only view of the live enemies (for rendering/tests).
    const std::vector<std::unique_ptr<Enemy>>& enemies() const;

    /// @return a modifiable view of the live enemies (for AI/combat/death
    ///         resolution to act on and prune).
    std::vector<std::unique_ptr<Enemy>>& enemies();

    /// Take ownership of a newly spawned enemy (used by the WaveManager).
    /// @param enemy an owning pointer to the enemy; appended to the live list.
    ///        Null pointers are ignored so the list never holds an empty slot.
    void addEnemy(std::unique_ptr<Enemy> enemy);

    // ---- Items (owned) ---------------------------------------------------

    /// @return a read-only view of the floor items (for rendering/tests).
    const std::vector<std::unique_ptr<Item>>& items() const;

    /// @return a modifiable view of the floor items (for placement/pickup).
    std::vector<std::unique_ptr<Item>>& items();

    /// Take ownership of an item placed on the floor.
    /// @param item an owning pointer to the item; appended to the item list.
    ///        Null pointers are ignored so the list never holds an empty slot.
    void addItem(std::unique_ptr<Item> item);

    // ---- Run counters ----------------------------------------------------

    /// @return the current 1-based Wave_Number.
    int waveNumber() const;

    /// Set the current Wave_Number (used by the WaveManager on advance, R17.4).
    /// @param newWaveNumber the wave index to store.
    void setWaveNumber(int newWaveNumber);

    /// @return the current Score.
    int score() const;

    /// Add to the run's Score (e.g. when Treasure is collected, R19.5/R24.1).
    /// @param amount the number of points to add (may be 0).
    void addScore(int amount);

    /// Set the Score to an exact value (used when recomputing per R24.1).
    /// @param newScore the score value to store.
    void setScore(int newScore);

    // ---- Gold (Feature 1: spendable currency) ---------------------------

    /// @return the player's current Gold reserve. Gold is the spendable
    /// currency awarded for collecting Treasure (in addition to the points
    /// it credits to Score) and consumed by purchases in the post-wave Shop
    /// (Feature 2). Score and Gold are tracked separately so the
    /// leaderboard formula keeps working unchanged while the player can still
    /// "spend" wealth between waves.
    int gold() const;

    /// Add gold to the player's reserve. Used by the Treasure pickup branch
    /// in TurnManager (Feature 1) so collecting a Treasure both feeds Score
    /// AND mints spendable currency. Negative deltas are ignored so a
    /// pickup can never silently subtract.
    /// @param amount the gold to add (any negative value is treated as 0).
    void addGold(int amount);

    /// Spend gold, clamping the reserve at 0 so a purchase can never drive
    /// the balance negative. Used by Shop purchases (Feature 2). Negative
    /// deltas are ignored.
    /// @param amount the gold to deduct (any negative value is treated as 0).
    void spendGold(int amount);

    /// Set the gold reserve to an exact value. Used by SaveManager during a
    /// transactional load to restore the saved balance.
    /// @param newGold the gold value to store; negative values are clamped to 0.
    void setGold(int newGold);

    /// @return the number of Turns elapsed in the run (R10.4).
    int turnCount() const;

    /// Advance the Turn_Count by one completed Turn (R10.4). The TurnManager
    /// calls this once both the player and enemy phases of a Turn finish.
    void incrementTurnCount();

    /// @return the number of enemies killed so far this run (R15.4/R24.1).
    int enemiesKilled() const;

    /// Record one more enemy kill (R15.4). Death resolution calls this for each
    /// enemy it removes from play.
    void incrementEnemiesKilled();

    // ---- Randomness ------------------------------------------------------

    /// @return a read-only reference to the run's Rng (e.g. to read the seed for
    ///         saving, R26.4).
    const Rng& rng() const;

    /// @return a modifiable reference to the run's Rng (so systems that need
    ///         randomness draw from the one shared, seeded generator) (R8.3).
    Rng& rng();

private:
    GridMap map_;                                ///< The dungeon grid for the wave.
    Player player_;                              ///< The hero (R8.3).
    std::vector<std::unique_ptr<Enemy>> enemies_;///< Live enemies (owned, R8.3).
    std::vector<std::unique_ptr<Item>> items_;   ///< Floor items (owned, R8.3).
    int waveNumber_;                             ///< Current 1-based Wave_Number.
    int score_;                                  ///< Accumulated run Score (R24.1).
    int gold_;                                   ///< Spendable Gold currency (Feature 1).
    int turnCount_;                              ///< Turns elapsed this run (R10.4).
    int enemiesKilled_;                          ///< Enemies killed so far (R15.4).
    Rng rng_;                                    ///< The run's deterministic Rng.
};

} // namespace dga
