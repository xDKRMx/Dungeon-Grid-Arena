// =============================================================================
// systems/AbilitySystem.cpp
//
// Purpose:
//   Definitions for the AbilitySystem class declared in systems/AbilitySystem.h.
//   This file wires the ACTUAL effects for Dash, Shield, Blink, and Nova into
//   the activate() dispatcher and implements tickCooldowns / addCharge (R21, R22).
//
// Layer: systems (depends on abilities, entities, world, systems/GameState,
//   systems/EventLog, core/Config).
// =============================================================================
#include "systems/AbilitySystem.h"

#include <algorithm> // std::max - clamp charge to 0; std::min - clamp to max.
#include <string>    // std::to_string - building log messages.
#include <vector>    // std::vector - collecting candidate Blink targets.

#include "abilities/Ability.h"       // Ability base - kind(), isReady(), tick(), putOnCooldown().
#include "abilities/BlinkAbility.h"  // BlinkAbility - identified by AbilityKind::Blink.
#include "abilities/DashAbility.h"   // DashAbility  - distance().
#include "abilities/NovaAbility.h"   // NovaAbility  - identified by AbilityKind::Nova.
#include "abilities/ShieldAbility.h" // ShieldAbility - durationTurns().
#include "core/Config.h"             // Config - chargeMeterMax() for Nova gate.
#include "core/Direction.h"          // allDirections() - 4-directional Chebyshev adjacency.
#include "core/Enums.h"              // AbilityKind, EntityKind.
#include "core/Vec2.h"               // Vec2 - positions and direction offsets.
#include "entities/Enemy.h"          // Enemy - for Nova adjacency damage.
#include "entities/Player.h"         // Player - position, chargeMeter, isShielded, applyShield.
#include "systems/EventLog.h"        // EventLog - log activation / rejection messages.
#include "systems/GameState.h"       // GameState - map, player, enemies.
#include "world/GridMap.h"           // GridMap - isWalkable for Dash.
#include "world/LineOfSight.h"       // LineOfSight::hasLineOfSight - Blink target validation.

namespace dga {

// =============================================================================
// activate — main dispatch
// =============================================================================

// Find the requested ability in the player's list, check rejection conditions,
// execute the effect, put the ability on cooldown, and return true.
// Returns false and logs a message if the ability is not found, on cooldown,
// or (for Nova) the Charge_Meter is not full.
bool AbilitySystem::activate(AbilityKind kind,
                             const Vec2& direction,
                             GameState& state,
                             const Config& config,
                             EventLog& log,
                             AbilityEffectInfo* outEffect) const {
    Player& player = state.player();

    // Search for the ability by kind in the player's ability list.
    Ability* foundAbility = nullptr;
    for (auto& abilityPtr : player.abilities()) {
        if (abilityPtr->kind() == kind) {
            foundAbility = abilityPtr.get();
            break;
        }
    }

    if (foundAbility == nullptr) {
        log.append("Ability not available.");
        return false;
    }

    // Reject if the ability is still on cooldown (R21.3).
    if (!foundAbility->isReady()) {
        log.append("Ability on cooldown — " +
                   std::to_string(foundAbility->cooldownRemaining()) + " turn(s) remaining.");
        return false;
    }

    // Extra rejection gate for Nova: require a full Charge_Meter (R22.3).
    if (kind == AbilityKind::Nova) {
        const int chargeMax = config.chargeMeterMax();
        if (player.chargeMeter() < chargeMax) {
            log.append("Nova requires a full Charge_Meter (" +
                       std::to_string(player.chargeMeter()) + "/" +
                       std::to_string(chargeMax) + ").");
            return false;
        }
    }

    // Dispatch to the concrete effect based on the ability kind. Using the
    // kind() tag rather than a dynamic_cast keeps the dispatch readable and
    // avoids RTTI cost on every activation (R8.5).
    switch (kind) {
        case AbilityKind::Dash: {
            // Down-cast to read the configured dash distance (R21.5).
            auto* dash = dynamic_cast<DashAbility*>(foundAbility);
            const int maxSteps = (dash != nullptr) ? dash->distance() : 1;
            applyDash(state, direction, maxSteps, log);
            break;
        }
        case AbilityKind::Shield: {
            // Down-cast to read the configured shield duration (R21.6).
            auto* shield = dynamic_cast<ShieldAbility*>(foundAbility);
            const int duration = (shield != nullptr) ? shield->durationTurns() : 1;
            applyShieldEffect(state, duration, log);
            break;
        }
        case AbilityKind::Blink:
            applyBlink(state, log);
            break;
        case AbilityKind::Nova:
            applyNova(state, config, log, outEffect);
            break;
        default:
            log.append("Unknown ability kind — no effect.");
            break;
    }

    // Record the cooldown so the ability cannot be reused immediately (R21.2).
    foundAbility->putOnCooldown();

    // ---- Blink Chain buff (Feature 2) -----------------------------------
    //
    // While the player holds Blink Chain charges, EACH Blink that actually
    // fires immediately clears its own cooldown so the buff strings together
    // (the player can Blink → Blink → Blink within the configured charge
    // count). The buff is only consumed by Blink uses — the other abilities
    // are unaffected. Implemented as a post-putOnCooldown override to keep
    // the activation pathway uniform: the standard cooldown is recorded
    // first, then nullified for Blink while charges remain.
    if (kind == AbilityKind::Blink &&
        player.blinkChainUsesRemaining() > 0) {
        foundAbility->clearCooldown();
        player.consumeBlinkChainUse();
        log.append("Blink Chain — cooldown skipped.");
    }

    return true;
}

// =============================================================================
// tickCooldowns — end-of-turn upkeep (R21.4)
// =============================================================================

// Decrement every ability's remaining cooldown by 1 (toward 0) and also tick
// the player's shield duration so both count down in sync.
void AbilitySystem::tickCooldowns(Player& player) const {
    for (auto& abilityPtr : player.abilities()) {
        abilityPtr->tick();
    }
    // Also tick the active shield each turn (R21.6).
    player.tickShield();
    // Tick the Fire-action cooldown so the per-shot pacing decays in lockstep
    // with ability cooldowns; the CombatSystem reads this counter on the next
    // Fire attempt to decide whether the shot is allowed.
    player.tickFireCooldown();
}

// =============================================================================
// addCharge — Charge_Meter management (R22.1)
// =============================================================================

// Clamp to [0, maxCharge] so the meter can never become negative and never
// exceeds its maximum (R22.1). The player's setChargeMeter stores the value.
void AbilitySystem::addCharge(Player& player, int amount, int maxCharge) const {
    if (amount <= 0) {
        return; // Negative increments are meaningless for a kill reward.
    }
    const int current    = player.chargeMeter();
    const int newCharge  = std::min(current + amount, maxCharge);
    player.setChargeMeter(newCharge);
}

// =============================================================================
// applyDash — Dash effect (R21.5)
// =============================================================================

// Move the player up to `maxSteps` tiles in `direction` along floor tiles.
// Each step is attempted in order; the loop stops as soon as the next tile
// would be a wall, off-map, or occupied by an enemy. An empty direction
// (Vec2{0,0}) results in no movement.
void AbilitySystem::applyDash(GameState& state,
                              const Vec2& direction,
                              int maxSteps,
                              EventLog& log) const {
    // A direction of {0,0} is a no-op (happens if the caller passes no direction).
    if (direction.x == 0 && direction.y == 0) {
        log.append("Dash: no direction given — no movement.");
        return;
    }

    Player& player    = state.player();
    const GridMap& map = state.map();
    Vec2 currentPos   = player.position();

    // Build a quick set of enemy-occupied tiles so we can treat them as blockers.
    // Using a vector here (small expected size) is fine.
    std::vector<Vec2> occupiedTiles;
    occupiedTiles.reserve(state.enemies().size());
    for (const auto& enemyPtr : state.enemies()) {
        occupiedTiles.push_back(enemyPtr->position());
    }

    int stepsTaken = 0;
    for (int step = 0; step < maxSteps; ++step) {
        const Vec2 nextPos = Vec2(currentPos.x + direction.x,
                                  currentPos.y + direction.y);

        // Stop at walls, map edges, or enemy-occupied tiles (R21.5).
        if (!map.isWalkable(nextPos)) {
            break;
        }

        // Check for an enemy blocking the destination tile.
        bool blocked = false;
        for (const Vec2& occupiedTile : occupiedTiles) {
            if (occupiedTile == nextPos) {
                blocked = true;
                break;
            }
        }
        if (blocked) {
            break;
        }

        // The step is valid: advance the player.
        currentPos = nextPos;
        ++stepsTaken;
    }

    // Apply the final position only once to avoid redundant position updates.
    player.setPosition(currentPos);

    log.append("Dash: moved " + std::to_string(stepsTaken) + " tile(s).");
}

// =============================================================================
// applyShieldEffect — Shield effect (R21.6)
// =============================================================================

// Grant the player timed damage immunity. CombatSystem::applyAttack checks
// player.isShielded() and absorbs the hit while the duration is nonzero (R21.6).
void AbilitySystem::applyShieldEffect(GameState& state,
                                      int durationTurns,
                                      EventLog& log) const {
    state.player().applyShield(durationTurns);
    log.append("Shield activated for " + std::to_string(durationTurns) + " turn(s).");
}

// =============================================================================
// applyBlink — Blink effect (R21.7)
// =============================================================================

// Collect all floor tiles that are:
//   1. Not occupied by the player or any enemy.
//   2. Visible from the player's current position (clear LOS, R21.7).
// Then pick one at random using the run's Rng and teleport the player there.
// Falls back to a log message if no valid target exists.
void AbilitySystem::applyBlink(GameState& state, EventLog& log) const {
    Player& player    = state.player();
    const GridMap& map = state.map();
    const Vec2 origin  = player.position();

    // Build the set of occupied tiles (player + enemies) to exclude from
    // Blink targets.
    std::vector<Vec2> occupied;
    occupied.push_back(origin); // player's own tile is occupied.
    for (const auto& enemyPtr : state.enemies()) {
        occupied.push_back(enemyPtr->position());
    }

    // Gather all floor tiles that are unoccupied and visible.
    const std::vector<Vec2> allFloor = map.floorTiles();
    std::vector<Vec2> candidates;
    candidates.reserve(allFloor.size());

    for (const Vec2& tile : allFloor) {
        // Exclude occupied tiles.
        bool isOccupied = false;
        for (const Vec2& occ : occupied) {
            if (occ == tile) {
                isOccupied = true;
                break;
            }
        }
        if (isOccupied) {
            continue;
        }

        // Include only tiles with clear line of sight from the player (R21.7).
        if (LineOfSight::hasLineOfSight(map, origin, tile)) {
            candidates.push_back(tile);
        }
    }

    if (candidates.empty()) {
        log.append("Blink: no visible empty floor tiles found — no teleport.");
        return;
    }

    // Pick a random candidate using the run's Rng (deterministic, R26.4).
    const Vec2& destination = state.rng().choice(candidates);
    player.setPosition(destination);
    log.append("Blink: teleported to (" + std::to_string(destination.x) +
               "," + std::to_string(destination.y) + ").");
}

// =============================================================================
// applyNova — Nova AoE ultimate (R22.2)
// =============================================================================

// Deal a large radial blast centred on the player. Every enemy whose Chebyshev
// (chessboard) distance from the hero is within config.novaRadius() is caught in
// the blast. A simple two-tier falloff makes it feel impactful without being
// uniform: enemies in the INNER ring (distance <= novaRadius/2) take the full
// configured novaDamage, while those in the OUTER ring take half. Nova ignores
// enemy armor entirely (it is a magical blast), so damage is applied directly
// via takeDamage rather than through CombatSystem::applyAttack.
//
// The Charge_Meter is reset to 0 regardless of how many enemies were hit, and
// the supplied AbilityEffectInfo (when non-null) is populated with the blast
// centre and radius so the renderer can draw the shockwave even on a blast that
// hits nobody — pressing 2 with a full meter ALWAYS shows the ultimate fire.
//
// NOTE: dead enemies are NOT removed here; that remains TurnManager's job via
// resolveDeaths after the action resolves.
void AbilitySystem::applyNova(GameState& state,
                              const Config& config,
                              EventLog& log,
                              AbilityEffectInfo* outEffect) const {
    Player& player    = state.player();
    const Vec2 origin = player.position();

    // Pull the blast geometry / damage from Config so no magic numbers live
    // here (R8.5). The inner-ring threshold is half the radius (integer
    // division), giving the two-tier falloff described above.
    const int novaRadius   = config.novaRadius();
    const int novaDamage   = config.novaDamage();
    const int innerRadius  = novaRadius / 2;
    const int outerDamage  = novaDamage / 2;

    int enemiesHit = 0;
    for (auto& enemyPtr : state.enemies()) {
        const Vec2 enemyPos = enemyPtr->position();
        const int chebyshev = origin.chebyshev(enemyPos);
        if (chebyshev > novaRadius) {
            continue; // Outside the blast entirely.
        }

        // Two-tier falloff: full damage in the inner ring, half in the outer.
        const int damage = (chebyshev <= innerRadius) ? novaDamage : outerDamage;
        enemyPtr->takeDamage(damage);
        log.append("Nova blast hits enemy at (" + std::to_string(enemyPos.x) +
                   "," + std::to_string(enemyPos.y) + ") for " +
                   std::to_string(damage) + " dmg.");
        ++enemiesHit;
    }

    // Reset the Charge_Meter to 0 regardless of how many enemies were hit (R22.2).
    player.setChargeMeter(0);
    log.append("NOVA BLAST! Hit " + std::to_string(enemiesHit) + " enemies.");

    // Report the blast to the renderer (when the caller asked for effect info)
    // so an expanding shockwave is drawn centred on the player — even when the
    // blast caught no enemies, so the ultimate is always visibly fired.
    if (outEffect != nullptr) {
        outEffect->novaFired  = true;
        outEffect->novaCenter = origin;
        outEffect->novaRadius = novaRadius;
    }
}

} // namespace dga
