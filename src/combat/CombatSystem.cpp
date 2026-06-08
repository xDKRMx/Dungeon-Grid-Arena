// =============================================================================
// combat/CombatSystem.cpp
//
// Purpose:
//   Definitions for the CombatSystem class declared in combat/CombatSystem.h.
//   This file contains the damage formula, the death-resolution erase-remove
//   loop, and the Bresenham-based projectile travel (R15, R16).
//
// Layer: combat (depends on entities, world, systems/EventLog, core).
// =============================================================================
#include "combat/CombatSystem.h"

#include <algorithm> // std::max   - clamp damage to 0; std::remove_if - erase dead.
#include <string>    // std::string - message construction.

#include "core/Vec2.h"         // Vec2 - projectile step arithmetic.
#include "core/Config.h"       // Config - playerFireCooldown() for cooldown reset.
#include "entities/Enemy.h"    // Enemy - needed for health() check and glyph().
#include "entities/Entity.h"   // Entity - attack/armor/takeDamage.
#include "entities/Player.h"   // Player - ammo, spendAmmo, position, glyph, isShielded.
#include "systems/EventLog.h"  // EventLog - append combat messages.
#include "world/GridMap.h"     // GridMap - inBounds and typeAt for projectile.
#include "world/LineOfSight.h" // LineOfSight::lineCells - projectile trajectory.
#include "core/Enums.h"        // TileType - wall check in projectile code.

namespace dga {

namespace {

// How far a projectile travels before it is considered a miss when no wall or
// enemy is hit first, as a SAFETY CAP only. The real stopping distance is the
// player's fireRange() (Feature 1); this constant just bounds the Bresenham ray
// length so it always covers any map dimension in the reference config even if
// a fire range were ever configured larger than the cap. Using a named constant
// avoids a magic number (R8.5).
constexpr int kMaxProjectileRange = 50;

/// Rotate a direction vector 45 degrees to the LEFT (counter-clockwise).
/// This produces an adjacent diagonal direction from a cardinal or diagonal
/// input. Used by the spread-shot mechanic to compute the two bonus beam
/// directions from the main firing direction.
/// @param dir the input direction Vec2 (unit step with components -1, 0, or 1).
/// @return the direction rotated 45 degrees counter-clockwise.
[[maybe_unused]] Vec2 rotateLeft45(const Vec2& dir) {
    // 45-degree CCW rotation matrix for integer unit vectors:
    //   x' = x*cos45 + y*sin45  →  for {0,-1,1} components we use:
    //   The standard 8-direction rotation: (dx, dy) -> ((dx+dy), (dy-dx))
    //   normalized to unit components by clamping to -1/0/1.
    const int rx = dir.x + dir.y;
    const int ry = dir.y - dir.x;
    // Clamp each component to {-1, 0, 1} so the result is a valid grid step.
    const int cx = (rx > 0) ? 1 : (rx < 0) ? -1 : 0;
    const int cy = (ry > 0) ? 1 : (ry < 0) ? -1 : 0;
    return Vec2(cx, cy);
}

/// Rotate a direction vector 45 degrees to the RIGHT (clockwise).
/// Produces the other adjacent diagonal direction for spread shot.
/// @param dir the input direction Vec2 (unit step with components -1, 0, or 1).
/// @return the direction rotated 45 degrees clockwise.
[[maybe_unused]] Vec2 rotateRight45(const Vec2& dir) {
    // 45-degree CW rotation: (dx, dy) -> ((dx-dy), (dy+dx))
    //   normalized to unit components by clamping to -1/0/1.
    const int rx = dir.x - dir.y;
    const int ry = dir.y + dir.x;
    const int cx = (rx > 0) ? 1 : (rx < 0) ? -1 : 0;
    const int cy = (ry > 0) ? 1 : (ry < 0) ? -1 : 0;
    return Vec2(cx, cy);
}

/// Build a short human-readable label for an entity (entity kind + glyph).
/// Used to make log messages more informative without pulling in a name string.
/// @param entity the entity to label.
/// @return a short string like "Player(@)" or "Enemy(M)".
std::string entityLabel(const Entity& entity) {
    char g = entity.glyph();
    switch (entity.kind()) {
        case EntityKind::Player:      return std::string("Player(") + g + ")";
        case EntityKind::MeleeEnemy:  return std::string("Melee(")  + g + ")";
        case EntityKind::RookEnemy:   return std::string("Rook(")   + g + ")";
        case EntityKind::BishopEnemy: return std::string("Bishop(") + g + ")";
        case EntityKind::QueenEnemy:  return std::string("Queen(")  + g + ")";
        case EntityKind::FastEnemy:   return std::string("Fast(")   + g + ")";
        case EntityKind::BossEnemy:   return std::string("Boss(")   + g + ")";
        default:                      return std::string("Entity(") + g + ")";
    }
}

} // namespace

// ---- applyAttack -----------------------------------------------------------

// Apply one hit from attacker to target.
//
// Armor-as-shield-buffer mechanic: armor is a SEPARATE health pool that
// absorbs damage before HP is touched. Raw damage = attacker.attack() with
// NO reduction. If the target has armor > 0, damage is absorbed from armor
// first; any remainder spills through to Health. When armor == 0, all damage
// goes straight to Health.
//
// Calls target.reduceArmor() and/or target.takeDamage() as needed.
// Logs attacker, target, damage dealt, armor absorbed, and target's remaining
// health (R15.5). If the target is the Player and the player is currently
// shielded (R21.6), the attack is fully absorbed — no damage is dealt and the
// block is logged.
//
// Twin Strike (Feature 2): when the ATTACKER is the Player AND the player has
// twinStrikeChargesRemaining() > 0, the raw damage is doubled and one charge
// is consumed. The buff applies to every attacker.attack() call routed
// through here, which is the path used by both the Phase 1 melee branch and
// any future melee that pipes through applyAttack — keeping the dispatch in
// this single function means Twin Strike is automatically picked up by
// anything that uses applyAttack rather than only the melee tile-walk branch.
void CombatSystem::applyAttack(Entity& attacker,
                               Entity& target,
                               EventLog& log) const {
    // Check for shield absorption when the target is the player (R21.6).
    if (target.kind() == EntityKind::Player) {
        // Down-cast is safe: we just confirmed the kind tag is Player.
        Player& targetPlayer = static_cast<Player&>(target);
        if (targetPlayer.isShielded()) {
            log.append(entityLabel(attacker) + " attacks " + entityLabel(target)
                       + " — blocked by shield!");
            return;
        }
    }

    // Raw damage starts at the attacker's full attack value — armor does NOT
    // reduce it; instead armor absorbs it as a separate buffer (armor-as-
    // shield-buffer).
    int rawDamage = attacker.attack();

    // ---- Twin Strike buff (Feature 2) -----------------------------------
    //
    // When the player attacks while a Twin Strike charge is active, double the
    // raw damage BEFORE armor absorption so the buff lands on both armor and
    // HP (the player is still hitting twice as hard, not just bypassing
    // armor). One charge is consumed per call so the 5-charge purchase covers
    // exactly five attacks. The buff is silently a no-op on enemy attacks
    // because the attacker kind gate keeps it out of the enemy phase.
    if (attacker.kind() == EntityKind::Player) {
        Player& attackerPlayer = static_cast<Player&>(attacker);
        if (attackerPlayer.twinStrikeChargesRemaining() > 0) {
            rawDamage *= 2;
            attackerPlayer.consumeTwinStrikeCharge();
            log.append("Twin Strike! Damage doubled.");
        }
    }

    if (target.armor() > 0) {
        // Armor absorbs as much damage as it can, then any remainder hits HP.
        const int armorAbsorbed   = std::min(rawDamage, target.armor());
        const int remainingDamage = rawDamage - armorAbsorbed;

        // Consume armor points from the shield buffer.
        target.reduceArmor(armorAbsorbed);

        // Spill remaining damage through to Health if armor didn't cover all.
        if (remainingDamage > 0) {
            target.takeDamage(remainingDamage);
        }

        // Build a descriptive log message showing the armor absorption (R15.5).
        log.append(entityLabel(attacker) + " hits " + entityLabel(target)
                   + " for " + std::to_string(rawDamage)
                   + " dmg (armor absorbed " + std::to_string(armorAbsorbed)
                   + ")  (HP " + std::to_string(target.health())
                   + "/" + std::to_string(target.maxHealth()) + ")");
    } else {
        // No armor — full damage goes straight to Health.
        target.takeDamage(rawDamage);

        // Standard log message without armor mention (R15.5).
        log.append(entityLabel(attacker) + " hits " + entityLabel(target)
                   + " for " + std::to_string(rawDamage)
                   + " dmg  (HP " + std::to_string(target.health())
                   + "/" + std::to_string(target.maxHealth()) + ")");
    }
}

// ---- resolveDeaths ---------------------------------------------------------

// Scan the enemy vector for dead entries, remove them, and update counters.
//
// Each dead enemy raises killCount by 1 and chargeMeter by 1 (clamped to max).
// If the player's health has also reached 0, playerDead is flagged (R10.6).
void CombatSystem::resolveDeaths(std::vector<std::unique_ptr<Enemy>>& enemies,
                                 Player& player,
                                 EventLog& log,
                                 int& killCount,
                                 int& chargeMeter,
                                 int  chargeMeterMax,
                                 bool& playerDead) const {
    // Walk backwards so that erasing by index does not skip entries.
    // Using erase-remove with an index loop is a clean alternative; we walk
    // backward to keep index arithmetic straightforward.
    for (int index = static_cast<int>(enemies.size()) - 1; index >= 0; --index) {
        if (enemies[static_cast<std::size_t>(index)]->health() <= 0) {
            // Log the death before removing the pointer (R15.5).
            log.append(entityLabel(*enemies[static_cast<std::size_t>(index)])
                       + " was defeated!");

            // Remove the dead enemy from the vector (R15.3).
            enemies.erase(enemies.begin() + index);

            // Increment the kill counter (R15.4).
            ++killCount;

            // Advance the Charge_Meter, clamped to its maximum (R22.1).
            ++chargeMeter;
            if (chargeMeter > chargeMeterMax) {
                chargeMeter = chargeMeterMax;
            }
        }
    }

    // Check for player death (R10.6). We only set the flag — transitioning to
    // Game Over is the caller's (TurnManager's) responsibility.
    if (player.health() <= 0) {
        playerDead = true;
    }
}

// ---- firePlayerProjectile --------------------------------------------------

// Trace a ray from the player in `direction`, spending ammo and damaging the
// first enemy hit. Rejects immediately when ammo == 0 (R16.3). Misses on wall
// or out-of-bounds (R16.4).
//
// Spread-shot mechanic: when the player has a ranged weapon equipped
// (hasSpreadShot()), the shot becomes OMNIDIRECTIONAL — beams are fired in
// every one of the eight surrounding directions (4 cardinal + 4 diagonal),
// regardless of which direction the caller passed. Bonus beams cost NO
// additional ammo and share the same cooldown as the main shot. Each beam
// independently damages the first enemy it hits.
//
// While walking, every cell the projectile passes through is recorded in
// `outResult.trail` so a graphical renderer can draw a tracer line + impact
// flash; the CombatSystem itself stays renderer-agnostic and stateless.
bool CombatSystem::firePlayerProjectile(
        const GridMap& map,
        Player& player,
        const Vec2& direction,
        std::vector<std::unique_ptr<Enemy>>& enemies,
        const Config& config,
        EventLog& log,
        FireResult& outResult) const {

    // Start the result struct from a clean slate so a previous call's data
    // never leaks across into this one (defensive even though Game.cpp passes
    // a fresh local each turn).
    outResult.fired = false;
    outResult.hit   = false;
    outResult.trail.clear();
    outResult.impact = Vec2(0, 0);

    // ---- Reject a zero-direction shot (FIX: fire-not-working bug) ----------
    if (direction.x == 0 && direction.y == 0) {
        log.append("Fire cancelled — no direction chosen.");
        return false;
    }

    // The config parameter is retained in the signature for API stability; the
    // cooldown duration is now read from the player (upgradeable). Suppress the
    // unused-parameter warning explicitly so -Wextra stays clean.
    (void)config;

    // Reject the shot when a fire cooldown is still ticking down (UX polish).
    if (player.fireCooldown() > 0) {
        log.append("Fire on cooldown — " +
                   std::to_string(player.fireCooldown()) +
                   " turn(s) remaining.");
        return false;
    }

    // Reject the shot if the player has no ammo (R16.3).
    if (player.ammo() == 0) {
        log.append("Player: out of ammo — shot rejected.");
        return false;
    }

    // Determine how far this shot may travel. The player's fireRange() is the
    // real stopping distance (Feature 1); kMaxProjectileRange is only a safety
    // cap so an unusually large configured range can never build a runaway ray.
    const int rawRange   = player.fireRange();
    const int safeRange  = (rawRange < 1) ? 1 : rawRange;
    const int travel     = (safeRange < kMaxProjectileRange) ? safeRange
                                                             : kMaxProjectileRange;

    // ---- Piercer Round buff (Feature 2) ---------------------------------
    //
    // While the player has a Piercer Round charge active, the projectile
    // passes THROUGH walls instead of stopping at them — only the map bounds
    // (and an enemy hit) end the ray. The buff is consumed once per shot
    // (regardless of whether any wall was actually pierced) so the 3-charge
    // purchase covers exactly three fires; this keeps the resource easy to
    // reason about and avoids "did this shot benefit?" edge-case bookkeeping.
    const bool piercingShot = (player.wallPierceShotsRemaining() > 0);
    if (piercingShot) {
        player.consumeWallPierceShot();
        log.append("Piercer Round active — projectile pierces walls.");
    }

    // ---- Defensive log: show exactly what the shot parameters are ----------
    log.append("Firing dir(" + std::to_string(direction.x) + ","
               + std::to_string(direction.y) + ") from ("
               + std::to_string(player.position().x) + ","
               + std::to_string(player.position().y) + ") range "
               + std::to_string(travel));

    // Compute the far end of the ray.
    const Vec2 origin  = player.position();
    const Vec2 farEnd  = Vec2(origin.x + direction.x * travel,
                              origin.y + direction.y * travel);

    const std::vector<Vec2> cells = LineOfSight::lineCells(origin, farEnd);

    // Always seed the trail with the player's own cell so renderers know where
    // the tracer originates.
    if (!cells.empty()) {
        outResult.trail.push_back(cells.front());
    }

    // ---- Main beam: walk the ray cell by cell, skipping the player's cell ---
    for (std::size_t cellIndex = 1; cellIndex < cells.size(); ++cellIndex) {
        const Vec2& cell = cells[cellIndex];

        // Stop at out-of-bounds first — even a piercing shot leaves the map.
        if (!map.inBounds(cell)) {
            player.spendAmmo(1);
            player.setFireCooldown(player.fireCooldownDuration());
            log.append("Player fires — projectile leaves the map.");
            outResult.trail.push_back(cell);
            outResult.impact = cell;
            outResult.fired  = true;
            outResult.hit    = false;
            // Fire bonus spread beams (no extra ammo cost) if equipped.
            goto fire_spread_beams;
        }

        // Stop at walls UNLESS a Piercer Round is active for this shot — in
        // which case the wall is recorded as a passed-through trail cell so
        // the renderer's tracer still draws over it but the ray continues.
        if (map.typeAt(cell) == TileType::Wall) {
            if (!piercingShot) {
                player.spendAmmo(1);
                player.setFireCooldown(player.fireCooldownDuration());
                log.append("Player fires — projectile hits a wall.");
                outResult.trail.push_back(cell);
                outResult.impact = cell;
                outResult.fired  = true;
                outResult.hit    = false;
                goto fire_spread_beams;
            }
            // Piercing: just record the cell and continue past the wall.
            outResult.trail.push_back(cell);
            continue;
        }

        // Check whether an enemy occupies this cell.
        for (auto& enemyPtr : enemies) {
            if (enemyPtr->position() == cell) {
                // For a normal shot, LOS must be clear (a wall between origin
                // and target absorbs the projectile). For a Piercer Round
                // shot the projectile already ignored walls, so LOS is
                // bypassed too — otherwise hitting an enemy hidden behind a
                // wall would still register as "blocked".
                if (piercingShot ||
                    LineOfSight::hasLineOfSight(map, origin, cell)) {
                    // Hit! Spend ammo and apply the attack (R16.1, R16.2).
                    player.spendAmmo(1);
                    player.setFireCooldown(player.fireCooldownDuration());
                    applyAttack(player, *enemyPtr, log);
                    outResult.trail.push_back(cell);
                    outResult.impact = cell;
                    outResult.fired  = true;
                    outResult.hit    = true;
                    goto fire_spread_beams;
                }
                // Enemy present but LOS blocked — projectile absorbed.
                player.spendAmmo(1);
                player.setFireCooldown(player.fireCooldownDuration());
                log.append("Player fires — line of sight blocked; projectile absorbed.");
                outResult.trail.push_back(cell);
                outResult.impact = cell;
                outResult.fired  = true;
                outResult.hit    = false;
                goto fire_spread_beams;
            }
        }
        // Cell is in-bounds, floor, unoccupied — continue the ray.
        outResult.trail.push_back(cell);
    }

    // The ray traversed its full fire range without hitting anything.
    player.spendAmmo(1);
    player.setFireCooldown(player.fireCooldownDuration());
    log.append("Player fires — projectile travels full range and misses.");
    if (!outResult.trail.empty()) {
        outResult.impact = outResult.trail.back();
    } else {
        outResult.impact = origin;
    }
    outResult.fired = true;
    outResult.hit   = false;

    // ===========================================================================
    // Spread-shot bonus beams (fires when a ranged weapon is equipped).
    // Fires SEVEN extra beams in every direction other than the main one — the
    // four cardinals plus the four diagonals — turning the shot into a 360°
    // omnidirectional burst. Each beam shares the main shot's ammo cost (1)
    // and cooldown (no extra resource is spent). Each bonus beam independently
    // damages the first enemy it hits. All bonus-beam cells are appended to
    // outResult.trail so the renderer draws them as part of the combined
    // tracer effect.
    // ===========================================================================
fire_spread_beams:
    if (player.hasSpreadShot()) {
        // The eight unit directions around the player. We fire one beam per
        // direction except the main `direction`, which has already been
        // processed by the main fire trace above.
        const Vec2 allEightDirs[] = {
            Vec2( 0, -1), // North
            Vec2( 1, -1), // North-East
            Vec2( 1,  0), // East
            Vec2( 1,  1), // South-East
            Vec2( 0,  1), // South
            Vec2(-1,  1), // South-West
            Vec2(-1,  0), // West
            Vec2(-1, -1)  // North-West
        };

        // Fire each bonus beam as an independent ray trace. We use a local
        // lambda to avoid duplicating the ray-walking logic for each side beam.
        // Each beam: trace from origin along bonusDir up to `travel` cells,
        // stopping at walls; damage the FIRST enemy hit; append cells to trail.
        auto fireBonusBeam = [&](const Vec2& bonusDir) {
            // Skip if the bonus direction ended up as (0,0) (shouldn't happen
            // with valid cardinal/diagonal inputs, but defensive).
            if (bonusDir.x == 0 && bonusDir.y == 0) { return; }

            const Vec2 bonusFarEnd = Vec2(origin.x + bonusDir.x * travel,
                                          origin.y + bonusDir.y * travel);
            const std::vector<Vec2> bonusCells =
                LineOfSight::lineCells(origin, bonusFarEnd);

            // Walk the bonus ray, skipping the player's own cell (index 0).
            for (std::size_t bi = 1; bi < bonusCells.size(); ++bi) {
                const Vec2& bCell = bonusCells[bi];

                // Stop at walls or out-of-bounds.
                if (!map.inBounds(bCell) ||
                    map.typeAt(bCell) == TileType::Wall) {
                    outResult.trail.push_back(bCell);
                    break;
                }

                // Check for an enemy on this cell.
                bool hitEnemy = false;
                for (auto& enemyPtr : enemies) {
                    if (enemyPtr->position() == bCell) {
                        if (LineOfSight::hasLineOfSight(map, origin, bCell)) {
                            // Bonus beam hits this enemy — apply damage.
                            applyAttack(player, *enemyPtr, log);
                            log.append("Spread shot bonus hit!");
                            outResult.hit = true; // At least one beam hit.
                        }
                        outResult.trail.push_back(bCell);
                        hitEnemy = true;
                        break;
                    }
                }
                if (hitEnemy) { break; }

                // Open floor — append to trail and continue.
                outResult.trail.push_back(bCell);
            }
        };

        // Fire one bonus beam per direction except the main `direction` (it
        // has already been traced above and is recorded in outResult.trail).
        for (const Vec2& d : allEightDirs) {
            if (d.x == direction.x && d.y == direction.y) {
                continue; // Skip — main beam already covers this direction.
            }
            fireBonusBeam(d);
        }
    }

    return true;
}

} // namespace dga
