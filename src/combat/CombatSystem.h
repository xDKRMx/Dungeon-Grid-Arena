// =============================================================================
// combat/CombatSystem.h
//
// Purpose:
//   CombatSystem is the stateless service that applies all combat arithmetic
//   to the game entities (R15, R16). It owns no state of its own; every
//   operation takes its inputs by reference and writes results back through
//   the supplied EventLog and the entity/enemy collections. Being stateless
//   makes it trivially testable: construct entities, call a function, inspect
//   the results.
//
//   Three public operations cover the full combat loop:
//     1. applyAttack   — apply one hit (attacker.attack − target.armor, floor 0)
//                        to a target and log the event (R15.2, R15.5).
//     2. resolveDeaths — scan the enemy list for dead entities, remove them,
//                        update kill count and Charge_Meter; detect player death
//                        and set a flag so the caller can transition to Game Over
//                        (R15.3, R15.4, R10.6, R22.1).
//     3. firePlayerProjectile — travel a ray in a direction using Bresenham line
//                        cells, spend 1 ammo, hit the first enemy on the path,
//                        reject on empty ammo with a log message (R16.1-16.4).
//
// Why a .h/.cpp split:
//   CombatSystem contains real logic (damage formula, ray-cast, death removal),
//   so declarations live here and definitions live in CombatSystem.cpp (R2.1).
//
// Layer: combat (depends on entities/Entity, entities/Player, entities/Enemy,
//   world/GridMap, world/LineOfSight, systems/EventLog, core/Vec2).
// =============================================================================
#pragma once

#include <memory>  // std::unique_ptr - enemy vector element type.
#include <vector>  // std::vector     - enemy list parameter.

#include "core/Vec2.h" // Vec2 - direction vector for firePlayerProjectile.

namespace dga {

// Forward declarations keep this header lean: CombatSystem.cpp includes the
// full definitions. All parameters below are taken by reference or pointer, so
// the header only needs to know the names, not the full types.
class Config;
class Entity;
class Enemy;
class EventLog;
class GridMap;
class Player;

// =============================================================================
// FireResult - structured outcome of a player projectile shot
// =============================================================================

/// One full description of what happened when the player tried to fire a
/// projectile (R16). The CombatSystem fills this in so the upstream layers can
/// report the shot back to the renderer for a transient visual effect (the
/// projectile trail and an impact flash) without having to re-derive the same
/// information from log text. Keeping this as a plain data carrier keeps the
/// CombatSystem itself stateless: it writes the result into the caller-supplied
/// struct and returns.
///
/// Field semantics:
///   * fired   - true if a shot actually went out (ammo spent), false if the
///               attempt was rejected (e.g. out of ammo, R16.3). When false,
///               trail/impact carry no meaningful data.
///   * hit     - true only when the projectile struck an enemy (damage applied);
///               false for walls, out-of-bounds, blocked LOS, or full-range miss.
///   * trail   - the cells the projectile traveled across, IN ORDER, INCLUDING
///               the impact cell as the final element. The very first cell is
///               the player's own tile; renderers should skip it when drawing.
///   * impact  - the last cell in trail; convenience copy so renderers do not
///               have to read trail.back() (and to keep a sensible value when
///               trail is empty for any defensive reason).
struct FireResult {
    bool              fired  = false;     ///< Did a shot actually fire?
    bool              hit    = false;     ///< Did it land on an enemy?
    std::vector<Vec2> trail;               ///< Cells traveled, player cell first.
    Vec2              impact{0, 0};        ///< Final cell in `trail` (mirror).
};

/// Stateless service that applies damage, death resolution, and projectile
/// combat (R15, R16). Construct once; all methods are const (no object state).
class CombatSystem {
public:
    /// Default constructor; the class holds no data members.
    CombatSystem() = default;

    // ---- Attack application (R15.2, R15.5) ---------------------------------

    /// Apply one melee or ranged hit from `attacker` to `target`.
    ///
    /// Armor-as-shield-buffer mechanic:
    ///   rawDamage = attacker.attack()  (no reduction — armor absorbs instead)
    ///   If target.armor() > 0:
    ///     armorAbsorbed = min(rawDamage, target.armor())
    ///     remainingDamage = rawDamage - armorAbsorbed
    ///     target.reduceArmor(armorAbsorbed)
    ///     if (remainingDamage > 0) target.takeDamage(remainingDamage)
    ///   Else:
    ///     target.takeDamage(rawDamage)
    ///
    /// Appends a descriptive message to `log` recording attacker name/kind,
    /// target name/kind, damage dealt, armor absorbed (if any), and target's
    /// resulting health (R15.5).
    ///
    /// @param attacker the entity making the attack (reads attack() stat).
    /// @param target   the entity receiving the hit (reads armor(), calls
    ///                 reduceArmor() and/or takeDamage()).
    /// @param log      the EventLog to record the hit message in.
    void applyAttack(Entity& attacker, Entity& target, EventLog& log) const;

    // ---- Death resolution (R15.3, R15.4, R10.6, R22.1) -------------------

    /// Remove dead enemies, update kill/charge counters, and flag player death.
    ///
    /// For each enemy in `enemies` whose health() <= 0:
    ///   - Remove it from the vector (erase-remove pattern).
    ///   - Increment `killCount` by 1 (R15.4).
    ///   - Increment `chargeMeter` by `chargeGainPerKill`, clamping to
    ///     `chargeMeterMax` (R22.1).
    ///   - Append a "{name} was defeated" message to `log` (R15.5).
    ///
    /// If `player.health() <= 0`, set `playerDead` to true (R10.6). The caller
    /// (TurnManager) reads `playerDead` to transition to Game Over.
    ///
    /// @param enemies        the live enemy list; dead enemies are erased.
    /// @param player         the hero; checked for player death.
    /// @param log            the EventLog to record death messages in.
    /// @param killCount      incremented by 1 for each dead enemy removed.
    /// @param chargeMeter    incremented by chargeGainPerKill per kill, then
    ///                       clamped to chargeMeterMax.
    /// @param chargeMeterMax the upper bound for chargeMeter (R22.1).
    /// @param playerDead     set to true when the player's health <= 0 (R10.6).
    void resolveDeaths(std::vector<std::unique_ptr<Enemy>>& enemies,
                       Player& player,
                       EventLog& log,
                       int& killCount,
                       int& chargeMeter,
                       int  chargeMeterMax,
                       bool& playerDead) const;

    // ---- Ranged player projectile (R16.1, R16.2, R16.3, R16.4) -----------

    /// Fire one projectile from the player along `direction`, hitting the first
    /// enemy in its path.
    ///
    /// Cooldown gate: if `player.fireCooldown() > 0` the shot is rejected
    /// BEFORE the ammo check. A short message naming the remaining turn count
    /// is logged, `outResult.fired` stays false (so the renderer draws no
    /// tracer), and the function returns false. This is what enforces the
    /// "two-turn cooldown between Fires" UX rule from Config::playerFireCooldown.
    ///
    /// Rejection: if the player's ammo is 0, append a "no ammo" message to `log`
    /// and return false immediately (R16.3). The player's position and ammo are
    /// NOT modified, and `outResult.fired` stays false.
    ///
    /// Trajectory: starting from player.position(), use LineOfSight::lineCells
    /// with (player.position(), player.position() + direction * kMaxProjectileRange)
    /// to enumerate cells along the direction. Each cell is checked in order:
    ///   - Out of bounds or Wall  → projectile misses (R16.4); log a miss message.
    ///   - Cell occupied by an enemy with clear LOS from player → hit that enemy
    ///     with applyAttack, spend 1 ammo (R16.1, R16.2), return true.
    ///   - Cell empty floor → continue.
    /// If the full ray is traced without hitting any enemy, log a miss.
    ///
    /// On a successful shot (ammo spent, regardless of hit/miss outcome), the
    /// player's fire cooldown is reset to `config.playerFireCooldown()` so the
    /// next Fire attempt is rejected until the configured number of Turns have
    /// elapsed (the AbilitySystem ticks the counter down each turn).
    ///
    /// As the ray walks, every cell it passes through (including the impact
    /// cell, whether that is an enemy, a wall, or the final out-of-range cell)
    /// is appended to `outResult.trail` and the final cell is mirrored into
    /// `outResult.impact`. `outResult.hit` is set true only on a real enemy hit.
    /// This lets a graphical renderer draw a tracer line + impact flash while
    /// the CombatSystem stays stateless and renderer-agnostic.
    ///
    /// @param map       the dungeon (used for bounds and wall checks).
    /// @param player    the shooter; position read, ammo spent on a hit,
    ///                  fire cooldown reset after a successful shot.
    /// @param direction a unit Vec2 step (+1/−1 in one or both axes) giving the
    ///                  firing direction; passed straight into lineCells.
    /// @param enemies   the live enemies; the first one on the path is damaged.
    /// @param config    balancing configuration; supplies the fire-cooldown
    ///                  duration applied after a successful shot.
    /// @param log       the EventLog to record the shot result in.
    /// @param outResult populated with the visual-effect info described above.
    /// @return true if a shot was fired (ammo available), false if rejected.
    bool firePlayerProjectile(const GridMap& map,
                              Player& player,
                              const Vec2& direction,
                              std::vector<std::unique_ptr<Enemy>>& enemies,
                              const Config& config,
                              EventLog& log,
                              FireResult& outResult) const;
};

} // namespace dga
