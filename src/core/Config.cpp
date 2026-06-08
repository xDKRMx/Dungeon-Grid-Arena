// =============================================================================
// core/Config.cpp
//
// Purpose:
//   Defines the default balancing values for the game and implements the const
//   getters declared in Config.h. This is the ONLY place these literals appear;
//   every other module reads them through the getters so there are no magic
//   numbers scattered through the logic (R8.5).
//
//   The default numbers below follow the requirements' guidance (for example
//   "Player ~100; standard enemies 20-40" from R15.1, scoring weights 100/10/1
//   from R24.1) and the locked decisions (4-directional movement, limited ammo,
//   boss substantially tougher than standard enemies per R18.2).
// =============================================================================
#include "core/Config.h"

namespace dga {

// -----------------------------------------------------------------------------
// Construction: assign every tunable its default value in one place.
// -----------------------------------------------------------------------------
Config::Config()
    // Grid dimensions: a comfortable arena that fits a terminal window.
    : gridWidth_(40),
      gridHeight_(24),

      // Player base stats (R15.1: Player ~100).
      playerStartingHealth_(100),
      playerMaxHealth_(100),
      playerStartingAttack_(14),   // Balanced: 2 fire shots to kill most enemies.
      playerStartingArmor_(0),
      playerStartingAmmo_(15),     // Generous enough for ~7 kills; ammo matters.

      // Player ranged fire range (Feature 1). The base reach is a deliberately
      // limited 6 cells so range matters tactically; picking up a ranged weapon
      // extends it by fireRangeUpgradeBonus_ (3) cells, making a kept ranged
      // weapon a clear reach upgrade. Both live here so the limit and the bonus
      // are never magic numbers in the combat code (R8.5).
      playerBaseFireRange_(6),
      fireRangeUpgradeBonus_(3),

      // Per-enemy-type base stats (R15.1: standard enemies 20-40).
      // Balanced so that 14 attack requires 2 shots to kill standard enemies:
      // 14 vs 25 HP = shot 1 leaves 11 HP, shot 2 kills. Tactical and fair.
      meleeEnemyHealth_(25),
      meleeEnemyAttack_(8),
      rookEnemyHealth_(22),
      rookEnemyAttack_(10),
      bishopEnemyHealth_(22),
      bishopEnemyAttack_(10),
      queenEnemyHealth_(30),   // Queen is tough: 3 shots to kill.
      queenEnemyAttack_(12),
      fastEnemyHealth_(18),    // Fast but not paper-thin — 2 shots to kill.
      fastEnemyAttack_(7),
      // Boss is substantially tougher than standard enemies (R18.2).
      bossEnemyHealth_(100),   // Focused boss fight; 8 shots to kill.
      bossEnemyAttack_(16),

      // Per-enemy-type firing range, in chessboard (Chebyshev) tiles.
      // Contact fighters reach exactly one tile; the chess-pattern shooters and
      // the boss reach across the arena along their lines (R14.2-R14.4).
      meleeEnemyRange_(1),
      rookEnemyRange_(8),
      bishopEnemyRange_(8),
      queenEnemyRange_(8),
      fastEnemyRange_(1),
      bossEnemyRange_(12),

      // Enemy behaviour tuning: a fast enemy covers two tiles a step (R12.3),
      // and the boss escalates through three attack phases over its Health bar
      // (R18.3).
      fastEnemyMovesPerTurn_(2),
      bossPhaseCount_(3),

      // Ranged enemy distance-weighted hit chance (Feature 3). A ranged enemy
      // is nearly certain to hit point-blank (95%) and loses only 8% per tile
      // of distance, never dropping below a 35% floor. At typical engagement
      // distance 4: 95 - 3*8 = 71% hit. At distance 8: 95 - 7*8 = 39%. This
      // makes ranged enemies a real threat the player must respect, rather than
      // harmless background noise that constantly misses (R8.5).
      rangedHitChanceAtRange1_(95),
      rangedHitChancePerTileFalloff_(8),
      rangedHitChanceMin_(35),

      // Ability cooldown durations, in Turns.
      dashCooldownDuration_(3),
      novaCooldownDuration_(0), // Nova is gated by the Charge_Meter, not a cooldown.
      shieldCooldownDuration_(5),
      blinkCooldownDuration_(4),

      // Ability magnitudes.
      dashDistance_(3),
      shieldDurationTurns_(4),  // Four turns of immunity — two full turn cycles.

      // Nova ultimate tuning (R22.2). The blast reaches a generous radius so
      // pressing 2 with a full Charge_Meter clears a large area around the hero
      // rather than only the eight adjacent tiles, making the ultimate feel like
      // a real screen-clearing payoff. Damage is high (an ultimate) and ignores
      // armor; a simple two-tier falloff (full damage in the inner half-radius,
      // half damage in the outer ring) keeps it impactful without being uniform.
      novaRadius_(5),
      novaDamage_(40),

      // Fire cooldown: 2 turns between consecutive shots (restored). The brief
      // wait window after each shot adds tactical weight to the Fire action —
      // the player must choose their shots carefully because they cannot spam
      // fire every turn. The "Rapid Fire" upgrade card can reduce this to 0
      // for unlimited fire speed later in the run (R8.5).
      playerFireCooldown_(2),

      // Charge meter: fills over roughly five kills, then Nova is available.
      // The per-kill gain was raised from 10 to 20 (five kills to fill instead
      // of ten) so the Nova ultimate becomes available at a snappier pace that
      // reads well in a short demo; the meter cap stays at 100 (R22.1).
      chargeMeterMax_(100),
      chargeGainPerKill_(20),

      // Scoring weights (R24.1: wave*100 + kills*10 + treasure*1).
      scoreWeightWave_(100),
      scoreWeightKill_(10),
      scoreWeightTreasure_(1),

      // User interface / persistence.
      eventLogDisplayCapacity_(6),
      highScoreFilePath_("data/highscores.txt"),
      saveFilePath_("data/savegame.txt") {}

// -----------------------------------------------------------------------------
// Grid dimensions.
// -----------------------------------------------------------------------------
int Config::gridWidth() const { return gridWidth_; }
int Config::gridHeight() const { return gridHeight_; }

// -----------------------------------------------------------------------------
// Player base stats.
// -----------------------------------------------------------------------------
int Config::playerStartingHealth() const { return playerStartingHealth_; }
int Config::playerMaxHealth() const { return playerMaxHealth_; }
int Config::playerStartingAttack() const { return playerStartingAttack_; }
int Config::playerStartingArmor() const { return playerStartingArmor_; }
int Config::playerStartingAmmo() const { return playerStartingAmmo_; }
int Config::playerBaseFireRange() const { return playerBaseFireRange_; }
int Config::fireRangeUpgradeBonus() const { return fireRangeUpgradeBonus_; }

// -----------------------------------------------------------------------------
// Per-enemy-type base stats.
//
// A small switch maps each enemy EntityKind to its configured value. Non-enemy
// kinds (e.g. Player) are not valid arguments; the default branch returns 0 so
// a misuse fails loudly-but-safely rather than reading uninitialized data.
// -----------------------------------------------------------------------------
int Config::enemyHealth(EntityKind enemyKind) const {
    switch (enemyKind) {
        case EntityKind::MeleeEnemy:  return meleeEnemyHealth_;
        case EntityKind::RookEnemy:   return rookEnemyHealth_;
        case EntityKind::BishopEnemy: return bishopEnemyHealth_;
        case EntityKind::QueenEnemy:  return queenEnemyHealth_;
        case EntityKind::FastEnemy:   return fastEnemyHealth_;
        case EntityKind::BossEnemy:   return bossEnemyHealth_;
        case EntityKind::Player:      return 0; // Not an enemy: no enemy stats.
    }
    return 0;
}

int Config::enemyAttack(EntityKind enemyKind) const {
    switch (enemyKind) {
        case EntityKind::MeleeEnemy:  return meleeEnemyAttack_;
        case EntityKind::RookEnemy:   return rookEnemyAttack_;
        case EntityKind::BishopEnemy: return bishopEnemyAttack_;
        case EntityKind::QueenEnemy:  return queenEnemyAttack_;
        case EntityKind::FastEnemy:   return fastEnemyAttack_;
        case EntityKind::BossEnemy:   return bossEnemyAttack_;
        case EntityKind::Player:      return 0; // Not an enemy: no enemy stats.
    }
    return 0;
}

// Firing range per enemy kind, in chessboard (Chebyshev) tiles. Contact kinds
// return 1; the chess shooters and boss return their longer reaches. As with the
// other lookups, a non-enemy kind safely returns 0.
int Config::enemyRange(EntityKind enemyKind) const {
    switch (enemyKind) {
        case EntityKind::MeleeEnemy:  return meleeEnemyRange_;
        case EntityKind::RookEnemy:   return rookEnemyRange_;
        case EntityKind::BishopEnemy: return bishopEnemyRange_;
        case EntityKind::QueenEnemy:  return queenEnemyRange_;
        case EntityKind::FastEnemy:   return fastEnemyRange_;
        case EntityKind::BossEnemy:   return bossEnemyRange_;
        case EntityKind::Player:      return 0; // Not an enemy: no enemy stats.
    }
    return 0;
}

// Enemy behaviour tuning getters.
int Config::fastEnemyMovesPerTurn() const { return fastEnemyMovesPerTurn_; }
int Config::bossPhaseCount() const { return bossPhaseCount_; }

// Ranged enemy distance-weighted hit chance getters (Feature 3).
int Config::rangedHitChanceAtRange1() const { return rangedHitChanceAtRange1_; }
int Config::rangedHitChancePerTileFalloff() const {
    return rangedHitChancePerTileFalloff_;
}
int Config::rangedHitChanceMin() const { return rangedHitChanceMin_; }

// -----------------------------------------------------------------------------
// Ability tuning.
// -----------------------------------------------------------------------------
int Config::abilityCooldownDuration(AbilityKind abilityKind) const {
    switch (abilityKind) {
        case AbilityKind::Dash:   return dashCooldownDuration_;
        case AbilityKind::Nova:   return novaCooldownDuration_;
        case AbilityKind::Shield: return shieldCooldownDuration_;
        case AbilityKind::Blink:  return blinkCooldownDuration_;
    }
    return 0;
}

int Config::dashDistance() const { return dashDistance_; }
int Config::shieldDurationTurns() const { return shieldDurationTurns_; }
int Config::novaRadius() const { return novaRadius_; }
int Config::novaDamage() const { return novaDamage_; }
int Config::playerFireCooldown() const { return playerFireCooldown_; }

// -----------------------------------------------------------------------------
// Charge meter.
// -----------------------------------------------------------------------------
int Config::chargeMeterMax() const { return chargeMeterMax_; }
int Config::chargeGainPerKill() const { return chargeGainPerKill_; }

// -----------------------------------------------------------------------------
// Scoring weights.
// -----------------------------------------------------------------------------
int Config::scoreWeightWave() const { return scoreWeightWave_; }
int Config::scoreWeightKill() const { return scoreWeightKill_; }
int Config::scoreWeightTreasure() const { return scoreWeightTreasure_; }

// -----------------------------------------------------------------------------
// User interface / persistence.
// -----------------------------------------------------------------------------
int Config::eventLogDisplayCapacity() const { return eventLogDisplayCapacity_; }
const std::string& Config::highScoreFilePath() const { return highScoreFilePath_; }
const std::string& Config::saveFilePath() const { return saveFilePath_; }

} // namespace dga
