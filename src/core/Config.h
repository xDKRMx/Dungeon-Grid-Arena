// =============================================================================
// core/Config.h
//
// Purpose:
//   Config is the single source of truth for every tunable constant in the
//   game (grid size, starting/maximum Health, per-enemy-type stats, ability
//   cooldowns and distances, Charge_Meter sizing, scoring weights, the
//   Event_Log display capacity, and the on-disk file paths).
//
//   Centralizing these values here removes "magic numbers" from the game logic
//   (R8.5): other modules ask Config for a value instead of hardcoding it, so
//   balancing the game means editing one file. All values are read through
//   const getters and are never mutated at runtime.
//
//   Unlike the other core files, Config has real logic (it maps enemy/ability
//   kinds to their stats), so it is split into a declaration header (this file)
//   and a definition source (Config.cpp) as required for non-trivial classes.
//
// Layer: core (depends only on core/Enums.h for the kind enumerations).
// =============================================================================
#pragma once

#include <string>

#include "core/Enums.h"

namespace dga {

/// Read-only container of balancing constants for one game configuration.
///
/// A Config instance is constructed with sensible defaults and then handed to
/// the systems that need it. Because every accessor is `const` and there are no
/// public mutators, a Config cannot change once built, which keeps balancing
/// values stable and predictable for the duration of a run.
class Config {
public:
    /// Construct a Config populated with the default balancing values.
    /// (The actual numbers are defined in Config.cpp so this header stays free
    /// of literals.)
    Config();

    // ---- Grid dimensions (R9.1) -------------------------------------------

    /// @return the dungeon grid width in tiles (number of columns).
    int gridWidth() const;

    /// @return the dungeon grid height in tiles (number of rows).
    int gridHeight() const;

    // ---- Player base stats (R15.1) ----------------------------------------

    /// @return the Health the Player starts a run with.
    int playerStartingHealth() const;

    /// @return the maximum Health the Player can ever be healed up to.
    int playerMaxHealth() const;

    /// @return the Player's base attack value before any weapon/upgrade.
    int playerStartingAttack() const;

    /// @return the Player's base armor (damage reduction) before any pickup.
    int playerStartingArmor() const;

    /// @return the amount of ammunition the Player begins a run with.
    int playerStartingAmmo() const;

    /// @return the Player's base ranged fire range, in cells, before any
    ///         upgrade. A fired projectile travels at most this many cells
    ///         before it is considered a miss (Feature 1). Keeping it here
    ///         removes the "effectively unlimited" 50-cell magic number from
    ///         the combat code (R8.5).
    int playerBaseFireRange() const;

    /// @return how many extra cells of fire range a ranged Weapon pickup grants
    ///         the Player on equip (Feature 1). Picking up a ranged weapon both
    ///         raises attack and extends fire range by this bonus, so a kept
    ///         ranged weapon reads as a clear reach upgrade.
    int fireRangeUpgradeBonus() const;

    // ---- Per-enemy-type base stats (R15.1, R18.2) -------------------------

    /// Look up the starting Health for a given enemy type.
    /// @param enemyKind the EntityKind of the enemy (must be an enemy kind).
    /// @return that enemy type's configured starting Health.
    int enemyHealth(EntityKind enemyKind) const;

    /// Look up the attack/contact damage for a given enemy type.
    /// @param enemyKind the EntityKind of the enemy (must be an enemy kind).
    /// @return that enemy type's configured attack value.
    int enemyAttack(EntityKind enemyKind) const;

    /// Look up the firing range (in chessboard / Chebyshev tiles) for a given
    /// enemy type. Contact fighters (Melee, Fast) have range 1; the chess-pattern
    /// ranged attackers (Rook, Bishop, Queen) and the Boss reach farther
    /// (R14.2-R14.4). Centralizing range here keeps the eligibility checks in the
    /// enemy code free of magic numbers (R8.5).
    /// @param enemyKind the EntityKind of the enemy (must be an enemy kind).
    /// @return that enemy type's configured firing range in tiles.
    int enemyRange(EntityKind enemyKind) const;

    /// @return how many tiles a Fast_Enemy advances in one action step (R12.3).
    ///         Kept in Config so the "fast" speed is not a magic number in the
    ///         entity code.
    int fastEnemyMovesPerTurn() const;

    /// @return how many phases a Boss_Enemy steps through over its life (R18.3).
    ///         The boss's Health bar is divided into this many equal bands; each
    ///         lower band raises the phase by one.
    int bossPhaseCount() const;

    // ---- Ranged enemy distance-weighted hit chance (Feature 3) ------------

    /// @return the percent (0-100) chance a RANGED enemy hits the player when
    ///         the player is exactly one tile away (Chebyshev distance 1). This
    ///         is the high end of the falloff curve: point-blank ranged shots
    ///         are nearly certain. Melee enemies (range == 1) ignore this and
    ///         always hit when adjacent.
    int rangedHitChanceAtRange1() const;

    /// @return the percent (0-100) of hit chance LOST per tile of distance
    ///         beyond the first (Feature 3). Effective chance is
    ///         rangedHitChanceAtRange1() - (distance-1) * this, clamped to
    ///         [rangedHitChanceMin(), 100]. Larger values make distant ranged
    ///         enemies miss more often, so the player can kite them.
    int rangedHitChancePerTileFalloff() const;

    /// @return the floor (0-100) the distance-weighted ranged hit chance never
    ///         drops below (Feature 3), so even a long-range shot retains a
    ///         small chance to connect.
    int rangedHitChanceMin() const;

    // ---- Ability tuning (R21, R22) ----------------------------------------

    /// Look up the cooldown duration (in Turns) for a given ability.
    /// @param abilityKind which ability to query.
    /// @return the number of Turns the ability stays on cooldown after use.
    int abilityCooldownDuration(AbilityKind abilityKind) const;

    /// @return how many tiles the Dash ability travels (R21.5).
    int dashDistance() const;

    /// @return how many Turns the Shield grants damage immunity (R21.6).
    int shieldDurationTurns() const;

    /// @return the radius (in Chebyshev/chessboard tiles) of the Nova ultimate
    ///         blast (R22.2). Every enemy whose Chebyshev distance from the
    ///         player is <= this value is caught in the blast, making Nova a
    ///         large screen-clearing ultimate rather than a single-ring hit.
    ///         Kept here so the blast size is never a magic number in the
    ///         AbilitySystem (R8.5).
    int novaRadius() const;

    /// @return the full Nova blast damage applied to enemies in the INNER ring
    ///         (Chebyshev distance <= novaRadius()/2). Enemies in the OUTER ring
    ///         take half this value (a simple two-tier falloff). Nova ignores
    ///         enemy armor entirely — it is a magical blast — so this is the raw
    ///         damage dealt. High by design because Nova is an ultimate (R22.2).
    int novaDamage() const;

    /// @return the cooldown (in Turns) imposed on the player after each
    ///         successful Fire action.
    ///
    /// Centralising this here keeps the "two-turn cooldown" out of the combat
    /// arithmetic as a magic number (R8.5). The CombatSystem reads this value
    /// once after a shot and writes it onto the player via setFireCooldown.
    int playerFireCooldown() const;

    // ---- Charge meter (ultimate resource) (R22) ---------------------------

    /// @return the full value of the Charge_Meter (Nova becomes usable here).
    int chargeMeterMax() const;

    /// @return how much the Charge_Meter gains for each enemy killed (R22.1).
    int chargeGainPerKill() const;

    // ---- Scoring weights (R24.1) ------------------------------------------

    /// @return points awarded per Wave_Number reached (default 100).
    int scoreWeightWave() const;

    /// @return points awarded per enemy killed (default 10).
    int scoreWeightKill() const;

    /// @return points awarded per unit of Treasure value collected (default 1).
    int scoreWeightTreasure() const;

    // ---- User interface / persistence -------------------------------------

    /// @return how many of the most recent Event_Log messages the HUD shows
    ///         (this is also the Event_Log's eviction capacity) (R6.2, R29.3).
    int eventLogDisplayCapacity() const;

    /// @return the file path the high-score leaderboard is read from / written
    ///         to (R3.1, R25.1).
    const std::string& highScoreFilePath() const;

    /// @return the file path a saved run is written to / restored from
    ///         (R3.2, R26.1).
    const std::string& saveFilePath() const;

private:
    // Grid dimensions.
    int gridWidth_;
    int gridHeight_;

    // Player base stats.
    int playerStartingHealth_;
    int playerMaxHealth_;
    int playerStartingAttack_;
    int playerStartingArmor_;
    int playerStartingAmmo_;
    int playerBaseFireRange_;    ///< Base ranged fire range in cells (Feature 1).
    int fireRangeUpgradeBonus_;  ///< Extra fire-range cells per ranged Weapon (Feature 1).

    // Per-enemy-type base stats.
    int meleeEnemyHealth_;
    int meleeEnemyAttack_;
    int rookEnemyHealth_;
    int rookEnemyAttack_;
    int bishopEnemyHealth_;
    int bishopEnemyAttack_;
    int queenEnemyHealth_;
    int queenEnemyAttack_;
    int fastEnemyHealth_;
    int fastEnemyAttack_;
    int bossEnemyHealth_;
    int bossEnemyAttack_;

    // Per-enemy-type firing range, in chessboard (Chebyshev) tiles.
    int meleeEnemyRange_;
    int rookEnemyRange_;
    int bishopEnemyRange_;
    int queenEnemyRange_;
    int fastEnemyRange_;
    int bossEnemyRange_;

    // Enemy behaviour tuning.
    int fastEnemyMovesPerTurn_; ///< Tiles a Fast_Enemy advances per step (R12.3).
    int bossPhaseCount_;        ///< Number of boss attack phases (R18.3).

    // Ranged enemy distance-weighted hit chance (Feature 3).
    int rangedHitChanceAtRange1_;       ///< Hit % at Chebyshev distance 1.
    int rangedHitChancePerTileFalloff_; ///< Hit % lost per extra tile of distance.
    int rangedHitChanceMin_;            ///< Floor the hit % never drops below.

    // Ability tuning.
    int dashCooldownDuration_;
    int novaCooldownDuration_;
    int shieldCooldownDuration_;
    int blinkCooldownDuration_;
    int dashDistance_;
    int shieldDurationTurns_;
    int novaRadius_; ///< Chebyshev radius of the Nova ultimate blast (R22.2).
    int novaDamage_; ///< Full (inner-ring) Nova blast damage; outer ring takes half (R22.2).
    int playerFireCooldown_; ///< Turns the Player must wait between consecutive Fire shots.

    // Charge meter.
    int chargeMeterMax_;
    int chargeGainPerKill_;

    // Scoring weights.
    int scoreWeightWave_;
    int scoreWeightKill_;
    int scoreWeightTreasure_;

    // User interface / persistence.
    int eventLogDisplayCapacity_;
    std::string highScoreFilePath_;
    std::string saveFilePath_;
};

} // namespace dga
