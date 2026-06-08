// =============================================================================
// systems/GameState.cpp
//
// Purpose:
//   Definitions for the GameState container declared in systems/GameState.h.
//   This is also the translation unit responsible for destroying the owned
//   collections, including the `unique_ptr<Enemy>` members. The destructor is
//   defined here (not in the header) precisely so that the point where an Enemy
//   would be deleted lives in a .cpp; once Enemy is implemented (task 5) this
//   file will include "entities/Enemy.h" so the deletion sees the full type.
//   At this stage no enemies are ever created, so the defaulted destructor
//   compiles cleanly with Enemy still incomplete.
//
//   GameState holds state only: the methods below store, expose, and adjust
//   data. No game rules (turn order, combat math, spawning) live here - those
//   belong to the systems that receive this object by reference (R8.3).
//
// Layer: systems (depends on world/GridMap, entities/Player, items/Item,
//   core/Rng, core/Config).
// =============================================================================
#include "systems/GameState.h"

#include "abilities/Ability.h" // full Ability type: Player's destructor, instantiated
                               // here when player_ is built/destroyed, must know how to
                               // delete the unique_ptr<Ability> abilities Player owns.
#include "core/Config.h"       // full type so the ctor can read starting values.
#include "entities/Enemy.h"    // full Enemy type (task 5): the out-of-line destructor
                               // below destroys the unique_ptr<Enemy> members, which
                               // requires Enemy to be a COMPLETE type at the point of
                               // deletion. With Enemy now implemented this include
                               // makes ~GameState() able to delete owned enemies.

namespace dga {

// Build the initial run state. The GridMap is sized from Config, the Player is
// constructed from Config at a placeholder origin cell (the real spawn is placed
// later by the generation/wave systems), and the Rng is seeded so the whole run
// is reproducible (R26.4). Wave_Number starts at 1 (the first wave); Score,
// Turn_Count, and the kill count all start at 0. No literals leak in beyond the
// documented starting wave and the placeholder origin.
GameState::GameState(const Config& config, unsigned int seed)
    : map_(config.gridWidth(), config.gridHeight()),
      player_(config, Vec2(0, 0)),
      enemies_(),
      items_(),
      waveNumber_(1),
      score_(0),
      gold_(0),
      turnCount_(0),
      enemiesKilled_(0),
      rng_(seed) {}

// Defaulted out-of-line (see the header / file purpose): destroying the
// unique_ptr<Enemy> members must happen where Enemy is complete. Keeping the
// definition in this .cpp is what makes owning a forward-declared Enemy legal.
GameState::~GameState() = default;

// Restore the GameState to its starting configuration in place. Mirrors what
// the constructor does field-by-field but operates on the already-constructed
// `*this`, so Game::resetRun no longer needs the placement-new dance to start
// a fresh run. The Config is the same one the constructor reads; `seed` is
// the new RNG seed for the upcoming run (R26.4).
void GameState::reset(const Config& config, unsigned int seed) {
    // Rebuild the dungeon grid (GridMap is value-type and assignable).
    map_ = GridMap(config.gridWidth(), config.gridHeight());

    // Reset the hero to its starting stats. The Player object stays alive;
    // only its fields are restored.
    player_.reset(config, Vec2(0, 0));

    // Drop everything from the previous run.
    enemies_.clear();
    items_.clear();

    // Reset every run counter to its starting value.
    waveNumber_    = 1;
    score_         = 0;
    gold_          = 0;
    turnCount_     = 0;
    enemiesKilled_ = 0;

    // Reseed the random source so a new seed produces a new run.
    rng_.reseed(seed);
}

// ---- Map -------------------------------------------------------------------

const GridMap& GameState::map() const {
    return map_;
}

GridMap& GameState::map() {
    return map_;
}

// ---- Player ----------------------------------------------------------------

const Player& GameState::player() const {
    return player_;
}

Player& GameState::player() {
    return player_;
}

// ---- Enemies (owned) -------------------------------------------------------

const std::vector<std::unique_ptr<Enemy>>& GameState::enemies() const {
    return enemies_;
}

std::vector<std::unique_ptr<Enemy>>& GameState::enemies() {
    return enemies_;
}

// Append a spawned enemy, ignoring null so the live list never carries an empty
// slot the AI/combat code would have to special-case. std::move transfers the
// unique_ptr's ownership into the vector.
void GameState::addEnemy(std::unique_ptr<Enemy> enemy) {
    if (enemy == nullptr) {
        return;
    }
    enemies_.push_back(std::move(enemy));
}

// ---- Items (owned) ---------------------------------------------------------

const std::vector<std::unique_ptr<Item>>& GameState::items() const {
    return items_;
}

std::vector<std::unique_ptr<Item>>& GameState::items() {
    return items_;
}

// Append a placed item, ignoring null for the same reason as addEnemy above.
void GameState::addItem(std::unique_ptr<Item> item) {
    if (item == nullptr) {
        return;
    }
    items_.push_back(std::move(item));
}

// ---- Run counters ----------------------------------------------------------

int GameState::waveNumber() const {
    return waveNumber_;
}

void GameState::setWaveNumber(int newWaveNumber) {
    waveNumber_ = newWaveNumber;
}

int GameState::score() const {
    return score_;
}

// Accumulate points. The amount is added as-is; the caller decides what a point
// is worth (e.g. a Treasure's value, R19.5). No clamping is needed since Score
// only ever grows during a run.
void GameState::addScore(int amount) {
    score_ += amount;
}

void GameState::setScore(int newScore) {
    score_ = newScore;
}

// ---- Gold (Feature 1) ------------------------------------------------------

// Report the player's spendable gold reserve. Treasure pickups feed BOTH the
// Score (leaderboard formula) and Gold (currency for the post-wave Shop), so
// the value tracked here is the live wallet — Score is incremented in lockstep
// but stored separately on its own field.
int GameState::gold() const {
    return gold_;
}

// Add to the gold reserve. Treasure pickups call this with the treasure value
// so the player accumulates spendable currency. Negative deltas are ignored
// (a pickup can never accidentally subtract); the reserve is unbounded above.
void GameState::addGold(int amount) {
    if (amount <= 0) {
        return;
    }
    gold_ += amount;
}

// Spend gold from the reserve, clamping at 0 so the wallet can never go
// negative. Negative deltas are treated as zero so a malformed call never
// silently increments the balance.
void GameState::spendGold(int amount) {
    if (amount <= 0) {
        return;
    }
    gold_ -= amount;
    if (gold_ < 0) {
        gold_ = 0;
    }
}

// Set the gold reserve to an exact value. Used by SaveManager to restore the
// balance during a transactional load. Negative inputs are clamped to 0 so the
// wallet stays in a valid state regardless of file content.
void GameState::setGold(int newGold) {
    gold_ = (newGold > 0) ? newGold : 0;
}

int GameState::turnCount() const {
    return turnCount_;
}

// One completed Turn = one increment (R10.4). The TurnManager calls this after
// both the player and enemy phases have run.
void GameState::incrementTurnCount() {
    turnCount_ += 1;
}

int GameState::enemiesKilled() const {
    return enemiesKilled_;
}

// One slain enemy = one increment (R15.4). Death resolution calls this per kill.
void GameState::incrementEnemiesKilled() {
    enemiesKilled_ += 1;
}

// ---- Randomness ------------------------------------------------------------

const Rng& GameState::rng() const {
    return rng_;
}

Rng& GameState::rng() {
    return rng_;
}

} // namespace dga
