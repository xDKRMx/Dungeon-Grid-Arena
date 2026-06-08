// =============================================================================
// systems/TurnManager.cpp
//
// Purpose:
//   Definitions for the TurnManager class declared in systems/TurnManager.h.
//   Implements the five-phase turn loop (R10.1): player action → enemy actions
//   → death resolution → end-of-turn checks → turn counter increment.
//
// Layer: systems (depends on combat, abilities, entities, world, core).
// =============================================================================
#include "systems/TurnManager.h"

#include <algorithm> // std::remove_if - defensive dead-enemy sweep in Phase 3.
#include <vector>    // std::vector - collecting blocked tiles for enemy pathfinding.
#include <string>    // std::to_string - building log messages.

#include "abilities/Ability.h"      // Ability - kind() accessor on ability objects.
#include "combat/CombatSystem.h"    // CombatSystem - applyAttack, resolveDeaths, firePlayerProjectile.
#include "core/Config.h"            // Config - chargeMeterMax(), chargeGainPerKill().
#include "core/Vec2.h"              // Vec2 - positions and direction arithmetic.
#include "entities/Enemy.h"         // Enemy, EnemyAction - decideAction.
#include "entities/Player.h"        // Player - position, setPosition, health accessors.
#include "systems/AbilitySystem.h"  // AbilitySystem - activate, tickCooldowns.
#include "systems/EventLog.h"       // EventLog - append messages.
#include "systems/GameState.h"      // GameState - authoritative state container.
#include "world/GridMap.h"          // GridMap - isWalkable, inBounds.
#include "world/Pathfinder.h"       // Pathfinder - passed to enemy decideAction.
#include "items/Item.h"             // Item - applyTo, isConsumable.
#include "items/Treasure.h"         // Treasure - value() for score crediting on pickup.

namespace dga {

namespace {

// ---- Helper: find an enemy at the given tile --------------------------------

/// Return a pointer to the first living enemy standing on `tile`, or nullptr
/// when no enemy is there. Used to detect "player moves into enemy tile" for the
/// melee-attack branch.
/// @param enemies the live enemy list.
/// @param tile    the tile to search for an enemy on.
/// @return pointer to the enemy or nullptr.
Enemy* enemyAt(const std::vector<std::unique_ptr<Enemy>>& enemies,
               const Vec2& tile) {
    for (const auto& enemyPtr : enemies) {
        if (enemyPtr->position() == tile) {
            return enemyPtr.get();
        }
    }
    return nullptr;
}

// ---- Helper: find a floor item at the given tile ----------------------------

/// Return a pointer to the first item lying on `tile`, or nullptr when none.
/// Used to detect "player walks onto an item tile" for the pickup branch.
/// @param items the floor item list owned by the GameState.
/// @param tile  the tile to search.
/// @return pointer to the item or nullptr.
Item* itemAt(const std::vector<std::unique_ptr<Item>>& items,
             const Vec2& tile) {
    for (const auto& itemPtr : items) {
        if (itemPtr->position() == tile) {
            return itemPtr.get();
        }
    }
    return nullptr;
}

// ---- Helper: human-readable item name for log messages ----------------------

/// Map an ItemKind to a short display name used in the "Picked up X" event-log
/// line so the player can see exactly what they collected (R29.3). Kept as a
/// switch (rather than a parallel array) so a future ItemKind addition forces a
/// compile consideration here.
/// @param kind the item kind to name.
/// @return a stable human-readable label for the item.
const char* itemDisplayName(ItemKind kind) {
    switch (kind) {
        case ItemKind::HealthPotion: return "Health Potion";
        case ItemKind::Weapon:       return "Weapon";
        case ItemKind::AmmoItem:     return "Ammo";
        case ItemKind::Armor:        return "Armor";
        case ItemKind::Treasure:     return "Treasure";
    }
    return "Item";
}

// ---- Helper: build blocked-tile list for enemy pathfinding ------------------

/// Collect every tile currently occupied by an entity (player + all enemies)
/// so enemy pathfinding routes AROUND allies rather than through them (R12.5).
/// The player tile is included only as a filler; the Pathfinder exempts the
/// goal tile, so passing it is harmless.
/// @param state the current game state.
/// @return vector of occupied tiles.
std::vector<Vec2> buildBlockedTiles(const GameState& state) {
    std::vector<Vec2> blocked;
    blocked.push_back(state.player().position());
    for (const auto& enemyPtr : state.enemies()) {
        blocked.push_back(enemyPtr->position());
    }
    return blocked;
}

} // anonymous namespace

// =============================================================================
// processTurn — the five-phase turn loop
// =============================================================================

TurnResult TurnManager::processTurn(GameState& state,
                                    const InputCommand& cmd,
                                    CombatSystem& combat,
                                    AbilitySystem& abilities,
                                    const Pathfinder& pathfinder,
                                    const Config& config,
                                    EventLog& log) const {

    // Initialise every TurnResult field explicitly; enemyAttacks starts empty
    // and is appended to during the enemy phase. (Listing the trailing members
    // keeps -Wmissing-field-initializers quiet.)
    TurnResult result{false, false, false, false, false, FireResult{}, {}};

    // -----------------------------------------------------------------------
    // Early-exit commands that do not consume a turn.
    // -----------------------------------------------------------------------
    if (cmd.type == InputCommand::Type::Quit) {
        result.quitRequested = true;
        return result;
    }
    if (cmd.type == InputCommand::Type::Save) {
        result.saveRequested = true;
        return result;
    }

    // -----------------------------------------------------------------------
    // PHASE 1 — Player action (R10.2, R11)
    // -----------------------------------------------------------------------
    // The player MUST choose exactly one action before the enemy phase begins.
    // A blocked move returns with turnConsumed = false so the caller can prompt
    // for another input (R11.2).

    bool playerActionConsumed = false;

    // Flag set ONLY when the player executed a SUCCESSFUL walking move
    // (open floor or pickup branch — NOT a melee tile attack). Used by the
    // Quickstep buff (Feature 2) to grant the player two movement actions
    // per turn while doubleMoveTurnsRemaining > 0: a successful walk does
    // not advance the enemy phase / turn counter, so the next pollInput
    // returns to the player for a second move within the same turn.
    bool playerWalked = false;

    if (cmd.type == InputCommand::Type::Wait) {
        // Wait advances the turn without any movement (R10.2).
        playerActionConsumed = true;

    } else if (cmd.type == InputCommand::Type::Move) {
        // Compute the destination tile.
        Player& player    = state.player();
        const Vec2 destination = Vec2(player.position().x + cmd.direction.x,
                                      player.position().y + cmd.direction.y);
        const GridMap& map     = state.map();

        // --- 11.2: Blocked move — reject (no turn consumed). ---
        if (!map.inBounds(destination) || !map.isWalkable(destination)) {
            log.append("Move blocked — wall or out-of-bounds.");
            result.turnConsumed = false;
            return result; // Return early; the caller re-prompts the player.
        }

        // --- 11.3: Move into an enemy tile → melee attack. ---
        Enemy* target = enemyAt(state.enemies(), destination);
        if (target != nullptr) {
            combat.applyAttack(player, *target, log);
            // Record the melee for visual feedback (Fix 4): the renderer draws
            // a brief red slash/X on the target cell so the player sees impact.
            result.playerMeleed       = true;
            result.playerMeleeTarget  = destination;
            playerActionConsumed = true;

        } else {
            // --- 11.4 / 11.1: Move to item tile or open floor. ---
            // Move the player first (R11.1, R11.4). Moving ONTO an item tile is
            // itself the pickup — no separate action is required, so stepping on
            // an item never wastes a turn.
            player.setPosition(destination);

            // Auto-collect ANY item on the destination tile. Previously only
            // consumables were removed from the floor, so weapons/armor lingered
            // on their tile and felt like they "weren't collected" when walked
            // over repeatedly. Now every item the hero stands on is collected as
            // part of the move and removed from the floor (R11.4, R19.6, R20.1).
            Item* pickedUp = itemAt(state.items(), destination);
            if (pickedUp != nullptr) {
                // Apply the item's effect to the player polymorphically (R19.6):
                // potions heal, ammo refills, armor/weapon adjust combat stats.
                pickedUp->applyTo(player);

                // Treasure is the one pickup whose effect is NOT a player stat:
                // its worth flows into the run Score (R19.5, R24.1). Treasure::
                // applyTo is a deliberate no-op, so the score must be credited
                // here from the treasure's value(). The kind tag lets us do this
                // without a dynamic_cast on the hot path.
                //
                // Feature 1 (Gold currency): the same value also mints
                // spendable Gold, so the player accumulates a wallet they can
                // empty in the post-wave Shop. Score keeps feeding the
                // leaderboard formula unchanged; Gold is a parallel,
                // independently tracked counter on GameState.
                if (pickedUp->kind() == ItemKind::Treasure) {
                    const Treasure* treasure =
                        static_cast<const Treasure*>(pickedUp);
                    state.addScore(treasure->value());
                    state.addGold(treasure->value());
                }

                // Record what was collected so the player sees it (R29.3).
                log.append(std::string("Picked up ") +
                           itemDisplayName(pickedUp->kind()) + ".");

                // Flag the pickup so Game.cpp can play the pickup chime via
                // the renderer's audio hook. (Audio is purely advisory; the
                // logic above already applied the item's effect.)
                result.itemPickedUp = true;

                // Remove the item from the floor list. ALL collected items leave
                // the floor now (not just consumables): the persistent effects
                // of weapons/armor are already baked into the player's stats by
                // applyTo above, so the floor object has served its purpose.
                // Erase by pointer identity from the GameState's owned list; any
                // matching inventory reference is dropped first so the inventory
                // never points at a freed item.
                auto& floorItems = state.items();
                for (auto it = floorItems.begin(); it != floorItems.end(); ++it) {
                    if (it->get() == pickedUp) {
                        player.inventory().remove(pickedUp);
                        floorItems.erase(it);
                        break;
                    }
                }
            }
            playerActionConsumed = true;
            // Mark this as a walking move so Quickstep (Feature 2) can grant
            // a second move within the same turn (see below).
            playerWalked = true;
        }

    } else if (cmd.type == InputCommand::Type::Fire) {
        // Delegate ranged attack to CombatSystem (R16). The combat system fills
        // a FireResult with the projectile's trail and impact cell so the
        // renderer can draw a transient tracer + impact flash this frame.
        FireResult fireOutcome;
        const bool shotFired = combat.firePlayerProjectile(state.map(),
                                    state.player(),
                                    cmd.direction,
                                    state.enemies(),
                                    config,
                                    log,
                                    fireOutcome);
        // Hand the visual data up to the caller (Game.cpp), which forwards it
        // to the renderer via IRenderer::showFireEffect.
        result.fireEffect = std::move(fireOutcome);

        // A rejected fire (no ammo, on cooldown, zero direction) does NOT
        // consume the turn so the player can try a different action instead of
        // being punished for a failed shot attempt.
        if (shotFired) {
            playerActionConsumed = true;
        } else {
            // Return immediately so the caller re-prompts the player (like a
            // blocked move). This prevents the enemy phase from running on a
            // turn where the player effectively did nothing.
            result.turnConsumed = false;
            return result;
        }

    } else if (cmd.type == InputCommand::Type::UseAbility) {
        // Find the ability by index and activate it via AbilitySystem (R21).
        const auto& playerAbilities = state.player().abilities();
        if (cmd.abilityIndex >= 0 &&
            static_cast<std::size_t>(cmd.abilityIndex) < playerAbilities.size()) {
            const AbilityKind kind = playerAbilities[static_cast<std::size_t>(
                                         cmd.abilityIndex)]->kind();
            // Pass a local AbilityEffectInfo so the AbilitySystem can report a
            // renderer-facing effect (currently the Nova blast). We copy the
            // Nova fields into the TurnResult so Game.cpp can drive the visual.
            AbilityEffectInfo effect;
            const bool fired = abilities.activate(kind, cmd.direction, state,
                                                  config, log, &effect);
            if (effect.novaFired) {
                result.novaFired  = true;
                result.novaCenter = effect.novaCenter;
                result.novaRadius = effect.novaRadius;
            }
            // Audio-cue for the renderer: only flag the activation when the
            // ability ACTUALLY fired (rejected casts return false from
            // activate, and we do not want to play a sound for a no-op).
            if (fired) {
                result.abilityActivated = true;
                result.abilityKind      = kind;
            }
        } else {
            log.append("UseAbility: invalid ability index.");
        }
        playerActionConsumed = true;
    }

    if (!playerActionConsumed) {
        // Unhandled command type — treat as a no-op but still consume the turn.
        playerActionConsumed = true;
    }

    // ---- Quickstep buff: grant a free additional move (Feature 2) ---------
    //
    // While the player holds Quickstep charges AND just completed a SUCCESSFUL
    // walking move (open floor or pickup, not melee), bypass the enemy phase
    // and the end-of-turn upkeep so the next pollInput returns to the player
    // for a second move within the same turn. One charge is consumed per
    // beneficial walk, so a 4-charge purchase grants up to four extra free
    // moves spread across consecutive turns. The fire/ability/wait branches
    // are unaffected since they do not set playerWalked.
    if (playerWalked && state.player().doubleMoveTurnsRemaining() > 0) {
        state.player().consumeDoubleMoveTurn();
        log.append("Quickstep — extra move granted.");
        result.turnConsumed = false; // Turn NOT advanced; player keeps control.
        return result;
    }

    // -----------------------------------------------------------------------
    // PHASE 2 — Enemy action phase (R10.3, R12.2-R12.4)
    // -----------------------------------------------------------------------
    // Each living enemy gets exactly one action step. FastEnemy may take up to
    // movesPerTurn() move steps within that one action (R12.3).

    for (auto& enemyPtr : state.enemies()) {
        if (!enemyPtr->isAlive()) {
            continue; // Skip already-dead enemies (resolveDeaths will remove them).
        }

        const Vec2 playerPos = state.player().position();
        const int stepsAllowed = enemyPtr->movesPerTurn();

        for (int step = 0; step < stepsAllowed; ++step) {
            // Rebuild blocked tiles each sub-step so updated positions are used.
            const std::vector<Vec2> blocked = buildBlockedTiles(state);

            const EnemyAction action =
                enemyPtr->decideAction(state.map(), playerPos, blocked, pathfinder);

            if (action.type == EnemyAction::Type::Attack) {
                // Build the visual-cue record up front; we capture the enemy's
                // CURRENT tile as the beam origin before its turn ends. An enemy
                // with firing range > 1 (Rook/Bishop/Queen/Boss) fired from a
                // distance → a ranged beam; range == 1 (Melee/Fast) is a contact
                // hit → a melee slash flash.
                EnemyAttackInfo attackInfo;
                attackInfo.enemyPos = enemyPtr->position();
                attackInfo.ranged   = (enemyPtr->range() > 1);
                attackInfo.hit      = true; // Assume a hit; ranged may downgrade.

                if (attackInfo.ranged) {
                    // Feature 3: a RANGED enemy's shot can MISS, with a chance
                    // that falls off with distance. Closer = more reliable,
                    // farther = easy to kite. Distance uses Chebyshev to match
                    // the chess firing geometry (a diagonal step counts as 1).
                    const int dist = enemyPtr->position().chebyshev(playerPos);

                    // hitChance = clamp(base - (dist-1)*falloff, min, 100).
                    const int base    = config.rangedHitChanceAtRange1();
                    const int falloff = config.rangedHitChancePerTileFalloff();
                    const int floorCh = config.rangedHitChanceMin();
                    int hitChance = base - (dist - 1) * falloff;
                    if (hitChance < floorCh) { hitChance = floorCh; }
                    if (hitChance > 100)     { hitChance = 100; }

                    // Roll [1, 100] against the chance; <= hitChance is a hit.
                    // Drawing from the shared run Rng keeps the outcome
                    // deterministic for a given seed (R26.4).
                    const int roll = state.rng().rangeInt(1, 100);
                    if (roll <= hitChance) {
                        // HIT: apply the attack as normal.
                        combat.applyAttack(*enemyPtr, state.player(), log);
                        attackInfo.hit = true;
                    } else {
                        // MISS: no damage, but still show the beam so the player
                        // sees the shot. The missed shot consumes the enemy's
                        // action (it fired this turn); the next time this enemy
                        // is eligible but cannot fire it will path closer, which
                        // raises its future hit chance.
                        log.append(std::string(1, enemyPtr->glyph()) +
                                   "-enemy fires and misses!");
                        attackInfo.hit = false;
                    }
                } else {
                    // Melee (range == 1) is reliable: always hits when adjacent.
                    combat.applyAttack(*enemyPtr, state.player(), log);
                    attackInfo.hit = true;
                }

                result.enemyAttacks.push_back(attackInfo);
                break; // Attack (hit or miss) ends this enemy's action step.

            } else if (action.type == EnemyAction::Type::Move) {
                // Validate that the target tile is actually walkable before moving
                // (defensive check; decideAction should always return valid moves).
                if (state.map().isWalkable(action.moveTo)) {
                    enemyPtr->setPosition(action.moveTo);
                }
                // For a non-Fast enemy stepsAllowed == 1 so the loop ends here.
                // For FastEnemy the loop continues until steps exhausted or attack
                // becomes eligible.

            } else {
                // Hold: no movement or attack; end this enemy's action.
                break;
            }
        }
    }

    // -----------------------------------------------------------------------
    // PHASE 3 — Death resolution (R15.3, R15.4, R10.6, R22.1)
    // -----------------------------------------------------------------------
    {
        bool playerDead    = false;

        // Use the GameState's stored chargeMeter via the player.
        int currentCharge  = state.player().chargeMeter();
        const int chargeMax = config.chargeMeterMax();
        const int chargeGain = config.chargeGainPerKill();

        // Remember whether the meter was already full BEFORE this turn's kills
        // so we can log the "Nova ready" hint exactly when a kill TOPS IT OFF
        // (the transition from not-full to full), rather than every turn the
        // meter merely sits at max (R22.1).
        const bool chargeWasFullBefore = (currentCharge >= chargeMax);

        // Walk backwards through the enemy list to remove dead enemies without
        // invalidating the index for remaining entries.
        auto& enemies = state.enemies();
        for (int idx = static_cast<int>(enemies.size()) - 1; idx >= 0; --idx) {
            const std::size_t uidx = static_cast<std::size_t>(idx);
            if (enemies[uidx]->health() <= 0) {
                log.append(std::string(1, enemies[uidx]->glyph()) +
                           "-enemy was defeated!");
                enemies.erase(enemies.begin() + idx);
                // Update kill counter through GameState's API (R15.4).
                state.incrementEnemiesKilled();
                // Add configured charge gain per kill, clamped to max (R22.1).
                currentCharge = std::min(currentCharge + chargeGain, chargeMax);
            }
        }

        // -----------------------------------------------------------------
        // Defensive sweep: catch any dead or null enemy entries that the
        // backward-erase loop above may have missed due to vector mutation
        // edge cases (e.g. boss-summoned minions inserted mid-iteration, or
        // a moved-from unique_ptr left as nullptr). This erase-remove_if
        // pass guarantees the vector contains ONLY living, valid enemies
        // before the wave-cleared check in Phase 5 runs.  Without this
        // guard the wave could fail to end even though every visible enemy
        // was killed — the reported "wave 6 won't clear" bug.
        // -----------------------------------------------------------------
        enemies.erase(
            std::remove_if(enemies.begin(), enemies.end(),
                [](const std::unique_ptr<Enemy>& e) {
                    // Remove null pointers (should never happen, but safe)
                    // and any enemy whose health has reached zero or below.
                    return !e || e->health() <= 0;
                }),
            enemies.end());

        state.player().setChargeMeter(currentCharge);

        // If this turn's kills just FILLED the Charge_Meter (it was not full
        // before and is now), surface a one-time on-screen hint so the player
        // knows the Nova ultimate is ready to unleash (R22.3, usability). The
        // not-full-before guard stops the message repeating on every later
        // kill while the meter stays pinned at max.
        if (!chargeWasFullBefore && currentCharge >= chargeMax) {
            log.append("Charge Meter FULL! Press 2 to unleash Nova.");
        }

        // Check for player death (R10.6).
        if (state.player().health() <= 0) {
            playerDead = true;
        }

        if (playerDead) {
            log.append("Player has died! Game over.");
            result.turnConsumed = true;
            result.playerDied   = true;
            return result;
        }
    }

    // -----------------------------------------------------------------------
    // PHASE 4 — End-of-turn checks (R21.4)
    // -----------------------------------------------------------------------
    // Tick ability cooldowns and shield duration via the AbilitySystem.
    abilities.tickCooldowns(state.player());

    // -----------------------------------------------------------------------
    // PHASE 5 — Increment turn counter and check wave clearance (R10.4, R10.5)
    // -----------------------------------------------------------------------
    state.incrementTurnCount();

    const bool waveCleared = state.enemies().empty();
    if (waveCleared) {
        log.append("Wave cleared!");
    }

    result.turnConsumed = true;
    result.waveCleared  = waveCleared;
    return result;
}

} // namespace dga
