// =============================================================================
// entities/Enemy.cpp
//
// Purpose:
//   Definitions for the Enemy abstract base and its six concrete subtypes
//   declared in entities/Enemy.h. This translation unit is where the project's
//   polymorphism actually does its work: each subtype supplies its own
//   canAttackPlayer() override implementing a chess piece's firing geometry, and
//   the shared decideAction() on the base turns those answers into a single
//   Attack / Move / Hold decision per turn (R12.2-R12.4, R14.1-R14.6).
//
//   It includes the FULL definitions the header only forward-declared - Config
//   (to read per-type stats), GridMap (walkability / walls), Pathfinder (the BFS
//   step), and LineOfSight (the Bresenham visibility check) - because the rule
//   bodies below call into all four. Keeping those includes in the .cpp (not the
//   header) is what holds the entities layer's compile-time coupling down.
//
// Geometry vocabulary used throughout (defined once in the anonymous namespace
// below so the rules read like their English descriptions):
//   * orthogonally adjacent  - Manhattan distance exactly 1 (a melee touch).
//   * same row or column     - dx == 0 OR dy == 0 (a rook's ranks and files).
//   * on a diagonal          - |dx| == |dy| (a bishop's diagonals).
//   * within range           - Chebyshev distance <= range_. Chebyshev counts a
//     diagonal step as 1, so for BOTH a straight line (one axis is 0) and a true
//     diagonal (both axes equal) it equals the number of tiles travelled along
//     that line - the natural "how many squares away am I?" metric for chess
//     movement (R14.2-R14.4).
//
// Layer: entities (depends on core/Config, world/GridMap, world/Pathfinder,
//   world/LineOfSight).
// =============================================================================
#include "entities/Enemy.h"

#include <algorithm> // std::max, std::min - phase clamping and minion capping.
#include <cstdlib>   // std::abs           - axis deltas for the geometry tests.

#include "core/Config.h"        // full type: constructors read per-type stats.
#include "world/GridMap.h"      // full type: walkability / walls for LOS & paths.
#include "world/LineOfSight.h"  // hasLineOfSight - the clear-line check (R14).
#include "world/Pathfinder.h"   // nextStepToward  - the one-tile BFS step (R12).

namespace dga {
namespace {

// ---- Geometry helpers (pure functions on two grid coordinates) -------------
// Each helper answers one chessboard relationship between an enemy tile `self`
// and the player's tile `player`. Pulling them out here keeps every
// canAttackPlayer() override short and self-documenting, and guarantees the rook
// and bishop rules combine into the queen's rule from the exact same predicates.

/// @return true when `self` and `player` are orthogonally adjacent (one
///         horizontal or vertical step apart) - the melee touch (R14.1).
bool orthogonallyAdjacent(const Vec2& self, const Vec2& player) {
    return self.manhattan(player) == 1;
}

/// @return true when `self` and `player` share a row or a column (a rook line).
///         Excludes the degenerate same-tile case so a piece never "attacks" its
///         own square.
bool sameRowOrColumn(const Vec2& self, const Vec2& player) {
    if (self == player) {
        return false;
    }
    return self.x == player.x || self.y == player.y;
}

/// @return true when `player` lies on a diagonal from `self` (|dx| == |dy|, a
///         bishop line). Excludes the same-tile case for the same reason as
///         sameRowOrColumn above.
bool onDiagonal(const Vec2& self, const Vec2& player) {
    if (self == player) {
        return false;
    }
    return std::abs(self.x - player.x) == std::abs(self.y - player.y);
}

} // namespace

// =============================================================================
// Enemy base
// =============================================================================

// Pull every starting number from Config so no balancing literal lives in the
// entity code (R8.5, R15.1). An enemy spawns at full Health, so starting Health
// and max Health are the same lookup; attack and contact damage are the same
// configured value; armor is 0 (standard enemies have no innate reduction).
Enemy::Enemy(EntityKind kind, const Vec2& position, const Config& config)
    : Entity(kind, position,
             config.enemyHealth(kind), config.enemyHealth(kind),
             config.enemyAttack(kind), /*armor=*/0),
      range_(config.enemyRange(kind)),
      contactDamage_(config.enemyAttack(kind)) {}

// Base firing rule (R4.6 fallback, R14.1): a bare hostile can only strike when
// the player is orthogonally adjacent. The `map` argument is unused at this base
// level because adjacency needs no wall check - it is part of the signature so
// the ranged subtypes that DO need the map can override with a matching
// prototype. The (void) cast documents the deliberate non-use and silences
// unused-parameter warnings.
bool Enemy::canAttackPlayer(const GridMap& map,
                            const Vec2& playerPosition) const {
    (void)map;
    return orthogonallyAdjacent(position_, playerPosition);
}

// The shared AI policy, implemented once for the whole family (Template Method):
// attack if the VIRTUAL canAttackPlayer says we may; otherwise if the enemy is
// right next to the player (Chebyshev distance 1) but cannot fire its ranged
// pattern, MELEE ATTACK as a contact-damage fallback — this prevents the Bishop
// (and any future ranged enemy) from awkwardly holding position when adjacent but
// off-axis. Otherwise take the first tile of the BFS route toward the player;
// otherwise hold. Because the eligibility call dispatches to the subtype, every
// enemy automatically gets AI that honours its own firing rule (R12.2, R12.4,
// R14.5).
EnemyAction Enemy::decideAction(const GridMap& map,
                                const Vec2& playerPosition,
                                const std::vector<Vec2>& blockedTiles,
                                const Pathfinder& pathfinder) const {
    // 1) Eligible to attack from here? Strike (R14.5 "fire if you can").
    if (canAttackPlayer(map, playerPosition)) {
        return EnemyAction{EnemyAction::Type::Attack, position_};
    }

    // 2) Melee fallback: if the enemy is adjacent (Chebyshev distance <= 1) to
    //    the player but canAttackPlayer returned false (e.g. Bishop not on its
    //    diagonal, Rook not on its axis), attack anyway as contact damage. This
    //    ensures every enemy is threatening at point-blank range regardless of
    //    its firing geometry — no more "stuck next to the player doing nothing".
    constexpr int MELEE_FALLBACK_DISTANCE = 1;
    if (position_.chebyshev(playerPosition) <= MELEE_FALLBACK_DISTANCE) {
        return EnemyAction{EnemyAction::Type::Attack, position_};
    }

    // 3) Otherwise step one tile along the shortest path toward the player.
    //    nextStepToward returns our own tile to signal "no path / already there"
    //    (see Pathfinder), so a returned step different from where we stand is a
    //    genuine move (R12.2).
    const Vec2 step =
        pathfinder.nextStepToward(map, position_, playerPosition, blockedTiles);
    if (step != position_) {
        return EnemyAction{EnemyAction::Type::Move, step};
    }

    // 4) No attack and no path: hold position for this step (R12.4).
    return EnemyAction{EnemyAction::Type::Hold, position_};
}

// Standard enemies advance exactly one tile per action step. FastEnemy overrides
// this; everyone else inherits the 1 (R12.2).
int Enemy::movesPerTurn() const {
    return 1;
}

int Enemy::range() const {
    return range_;
}

int Enemy::contactDamage() const {
    return contactDamage_;
}

// =============================================================================
// MeleeEnemy - contact fighter (R14.1), glyph 'M'
// =============================================================================

// Public constructor: a plain melee enemy tagged EntityKind::MeleeEnemy.
MeleeEnemy::MeleeEnemy(const Vec2& position, const Config& config)
    : Enemy(EntityKind::MeleeEnemy, position, config) {}

// Protected constructor: lets a derived melee fighter (FastEnemy) reuse the
// adjacency behaviour while spawning under its own kind tag (R8.5 - stats still
// come from Config for that kind).
MeleeEnemy::MeleeEnemy(EntityKind kind, const Vec2& position,
                       const Config& config)
    : Enemy(kind, position, config) {}

// A melee strike depends only on adjacency - never on range or line of sight -
// so we test exactly that and ignore the map (R14.1). Restating the rule here
// (rather than leaning on the inherited base) documents the melee contract at
// the type that owns it and at the base FastEnemy builds on.
bool MeleeEnemy::canAttackPlayer(const GridMap& map,
                                 const Vec2& playerPosition) const {
    (void)map;
    return orthogonallyAdjacent(position_, playerPosition);
}

char MeleeEnemy::glyph() const {
    return 'M';
}

// =============================================================================
// RookEnemy - fires along ranks and files (R14.2), glyph 'R'
// =============================================================================

RookEnemy::RookEnemy(const Vec2& position, const Config& config)
    : Enemy(EntityKind::RookEnemy, position, config) {}

// Eligible exactly when the three rook conditions all hold: the player shares a
// row or column, is within firing range (Chebyshev <= range_), and the straight
// line between the two tiles is unobstructed by walls (R14.2). The conditions are
// ordered cheap-to-expensive so the LOS walk only runs when the geometry and
// range already match.
bool RookEnemy::canAttackPlayer(const GridMap& map,
                                const Vec2& playerPosition) const {
    return sameRowOrColumn(position_, playerPosition) &&
           position_.chebyshev(playerPosition) <= range_ &&
           LineOfSight::hasLineOfSight(map, position_, playerPosition);
}

char RookEnemy::glyph() const {
    return 'R';
}

// =============================================================================
// BishopEnemy - fires along diagonals (R14.3), glyph 'B'
// =============================================================================

BishopEnemy::BishopEnemy(const Vec2& position, const Config& config)
    : Enemy(EntityKind::BishopEnemy, position, config) {}

// Eligible when the player lies on a diagonal (|dx| == |dy|), within range, with
// a clear diagonal line (R14.3). Same cheap-to-expensive ordering as the rook.
bool BishopEnemy::canAttackPlayer(const GridMap& map,
                                  const Vec2& playerPosition) const {
    return onDiagonal(position_, playerPosition) &&
           position_.chebyshev(playerPosition) <= range_ &&
           LineOfSight::hasLineOfSight(map, position_, playerPosition);
}

char BishopEnemy::glyph() const {
    return 'B';
}

// =============================================================================
// QueenEnemy - fires along ranks, files, AND diagonals (R14.4), glyph 'Q'
// =============================================================================

QueenEnemy::QueenEnemy(const Vec2& position, const Config& config)
    : Enemy(EntityKind::QueenEnemy, position, config) {}

// The queen rule is the UNION of the rook and bishop geometries, sharing the
// same range and LOS gates (R14.4). Reusing the very same predicates the rook
// and bishop use guarantees the three rules stay consistent with one another.
bool QueenEnemy::canAttackPlayer(const GridMap& map,
                                 const Vec2& playerPosition) const {
    const bool aligned = sameRowOrColumn(position_, playerPosition) ||
                         onDiagonal(position_, playerPosition);
    return aligned &&
           position_.chebyshev(playerPosition) <= range_ &&
           LineOfSight::hasLineOfSight(map, position_, playerPosition);
}

char QueenEnemy::glyph() const {
    return 'Q';
}

// =============================================================================
// FastEnemy - a MeleeEnemy that moves up to two tiles per step (R12.3), 'F'
// =============================================================================

// Constructs through MeleeEnemy's protected constructor so it inherits the
// adjacency attack rule unchanged (it provides NO canAttackPlayer override - a
// live demonstration of the R4.6 fallback) while spawning as EntityKind::Fast.
// Its per-step movement allowance is read from Config (no magic number, R8.5).
FastEnemy::FastEnemy(const Vec2& position, const Config& config)
    : MeleeEnemy(EntityKind::FastEnemy, position, config),
      movesPerTurn_(config.fastEnemyMovesPerTurn()) {}

int FastEnemy::movesPerTurn() const {
    return movesPerTurn_;
}

char FastEnemy::glyph() const {
    return 'F';
}

// =============================================================================
// BossEnemy - multi-phase queen-pattern attacker that summons minions (R18), 'X'
// =============================================================================

// Boss stats come from Config's dedicated boss entries, which are substantially
// larger than a standard enemy's (R18.2). The boss begins in phase 1 at full
// Health (R18.3).
BossEnemy::BossEnemy(const Vec2& position, const Config& config)
    : Enemy(EntityKind::BossEnemy, position, config),
      phase_(1),
      phaseCount_(config.bossPhaseCount()) {}

// The boss attacks with the most dangerous geometry - the queen's full
// row/column/diagonal reach, gated by range and LOS (R14.4 applied to the boss).
bool BossEnemy::canAttackPlayer(const GridMap& map,
                                const Vec2& playerPosition) const {
    const bool aligned = sameRowOrColumn(position_, playerPosition) ||
                         onDiagonal(position_, playerPosition);
    return aligned &&
           position_.chebyshev(playerPosition) <= range_ &&
           LineOfSight::hasLineOfSight(map, position_, playerPosition);
}

int BossEnemy::phase() const {
    return phase_;
}

int BossEnemy::phaseCount() const {
    // Captured from Config at construction so the boss reports the same phase
    // count it was built with - no reconstruction, no magic number (R8.5).
    return phaseCount_;
}

// Map the boss's CURRENT Health onto a 1-based phase by splitting [0, maxHealth]
// into phaseCount() equal bands counted from the top: full Health is phase 1 and
// Health near 0 is the final phase (R18.3). Worked through with N bands:
//   target = N - floor(health * N / maxHealth), clamped into [1, N].
// At exactly a band boundary the boss is still in the HIGHER phase (it advances
// only once Health drops strictly BELOW the threshold), matching "drops below a
// configured phase threshold" in R18.3.
int BossEnemy::phaseForCurrentHealth() const {
    const int totalPhases = phaseCount();

    // Guard against a degenerate configuration so we never divide by zero; with
    // no Health bar to divide, treat the boss as still in its opening phase.
    if (maxHealth() <= 0 || totalPhases <= 1) {
        return 1;
    }

    // Integer band arithmetic: multiply before dividing to avoid truncation bias.
    const int band = (health() * totalPhases) / maxHealth();
    const int target = totalPhases - band;

    // Clamp into [1, totalPhases]: full Health yields band == totalPhases (target
    // 0 -> 1), and any Health at/under 0 yields the lowest phase.
    return std::max(1, std::min(target, totalPhases));
}

// Advance the stored phase to match current Health, but only ever UPWARD. Taking
// the max of the old and target phases makes the phase strictly MONOTONIC: even
// if the boss were healed back up, its phase never regresses (R18.3).
bool BossEnemy::updatePhase() {
    const int target = phaseForCurrentHealth();
    const int advanced = std::max(phase_, target);
    if (advanced != phase_) {
        phase_ = advanced;
        return true;
    }
    return false;
}

// Summon up to `phase()` melee minions onto the supplied floor tiles - more
// minions in later phases - capped by the number of tiles actually available
// (R18.4). The boss builds the minion objects and hands ownership to the caller;
// it is the WaveManager (systems layer) that drops them into the GameState, so
// the entities layer stays free of any systems dependency (R8.1).
std::vector<std::unique_ptr<Enemy>> BossEnemy::summonMinions(
    const std::vector<Vec2>& spawnTiles, const Config& config) const {
    std::vector<std::unique_ptr<Enemy>> minions;

    // Want one minion per current phase, but never more than we have tiles for.
    const int desired = phase_;
    const int available = static_cast<int>(spawnTiles.size());
    const int count = std::min(desired, available);

    minions.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        minions.push_back(
            std::make_unique<MeleeEnemy>(spawnTiles[static_cast<std::size_t>(index)],
                                         config));
    }
    return minions;
}

char BossEnemy::glyph() const {
    return 'X';
}

} // namespace dga
