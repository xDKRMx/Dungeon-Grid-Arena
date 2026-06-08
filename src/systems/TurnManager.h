// =============================================================================
// systems/TurnManager.h
//
// Purpose:
//   TurnManager implements the strict turn order mandated by R10.1:
//
//     1. Player action phase
//     2. Enemy action phase
//     3. Death resolution
//     4. End-of-turn checks (cooldowns, shield tick, wave-clear flag)
//     5. Turn counter increment
//
//   The central function processTurn() receives the player's chosen
//   InputCommand and carries out all five phases in sequence. It returns a
//   TurnResult that tells the caller whether the wave was cleared, the player
//   died, or play simply continues.
//
//   Player action resolution (R11, R20.1):
//     * Move toward a Wall or off-map → rejected; no turn consumed (R11.2).
//     * Move toward an enemy tile → melee attack on that enemy (R11.3).
//     * Move toward an item tile → move player then apply item effect; remove
//       from the floor if the item is consumable (R11.4, R20.1, R20.3).
//     * Move toward empty floor → move the player (R11.1).
//     * UseAbility → delegate to AbilitySystem (R21).
//     * Fire → delegate to CombatSystem::firePlayerProjectile (R16).
//     * Wait → advance the turn without player movement (R10.2).
//     * Quit / Save → signal the caller; no turn consumed.
//
//   Enemy action phase (R10.3, R12.2-R12.4):
//     For each living enemy, call decideAction; then:
//       * Attack → call CombatSystem::applyAttack(enemy, player).
//       * Move   → set enemy position to moveTo.
//       * Hold   → do nothing.
//     FastEnemy has movesPerTurn() > 1: the Move step is repeated up to that
//     many times within the same enemy's action step, stopping early if the
//     enemy becomes eligible to attack (R12.3).
//
// InputCommand struct:
//   Defined here (not in a separate header) because it is the exclusive input
//   contract between the renderer and the turn loop. The Type enum covers every
//   meaningful player action so the system never needs to inspect raw keycodes.
//
// Requirements: 10.1, 10.2, 10.3, 10.4, 10.5, 11.1, 11.2, 11.3, 11.4, 20.1
//
// Why a .h/.cpp split:
//   TurnManager contains real orchestration logic; declarations live here and
//   definitions in TurnManager.cpp (R2.1).
//
// Layer: systems (depends on combat, abilities, entities, world, core).
// =============================================================================
#pragma once

#include "core/Enums.h"            // AbilityKind - carried in InputCommand.
#include "core/Vec2.h"             // Vec2        - direction / destination in InputCommand.
#include "combat/CombatSystem.h"   // FireResult  - returned through TurnResult.

#include <vector> // std::vector - list of per-enemy attack effects in TurnResult.

namespace dga {

class AbilitySystem;
class CombatSystem;
class Config;
class EventLog;
class GameState;
class Pathfinder;

// =============================================================================
// InputCommand — the abstract action the player chose for this turn.
// =============================================================================

/// The complete description of one player action, as produced by the renderer
/// and consumed by TurnManager::processTurn.
///
/// The Type enum covers every possible player intent so the turn loop never
/// needs to inspect raw keycodes or renderer-specific state (R8.2).
struct InputCommand {
    /// The category of action the player chose.
    enum class Type {
        Move,       ///< Move one tile in `direction` (R11).
        Fire,       ///< Fire a projectile in `direction` (R16).
        UseAbility, ///< Activate the ability at `abilityIndex` with `direction`
                    ///< as the optional directional parameter (R21).
        Wait,       ///< Skip this action phase without consuming a turn move (R10.2).
        Quit,       ///< Request to quit; no turn is consumed.
        Save        ///< Request to save; no turn is consumed.
    };

    Type type;         ///< The chosen action category.
    Vec2 direction;    ///< Unit direction for Move/Fire/UseAbility; {0,0} for others.
    int abilityIndex;  ///< 0-based index into player.abilities() for UseAbility;
                       ///< ignored for other action types.

    /// Convenience constructor: build the most common actions cleanly.
    /// @param t    the action type.
    /// @param dir  the direction (default {0,0}).
    /// @param idx  the ability index (default 0).
    explicit InputCommand(Type t,
                          const Vec2& dir = Vec2(0, 0),
                          int idx = 0)
        : type(t), direction(dir), abilityIndex(idx) {}
};

// =============================================================================
// EnemyAttackInfo — one enemy's attack on the player this turn (visual cue)
// =============================================================================

/// Records a single enemy attack that landed on the player during the enemy
/// phase, so the caller can ask the renderer to draw a brief visual cue for it
/// (a ranged beam from the enemy to the hero, or a melee slash flash on the
/// hero's tile). This is purely advisory render data — the actual damage was
/// already applied by the CombatSystem when the attack resolved.
///
/// Field semantics:
///   * enemyPos — the tile the attacking enemy occupied when it struck; the
///                origin of a ranged beam and the source of a melee lunge.
///   * ranged   — true when the attacker fires from a distance (range > 1:
///                Rook / Bishop / Queen / Boss), false for a contact attack
///                (range == 1: Melee / Fast). Lets the renderer choose between
///                a beam and a slash flash.
struct EnemyAttackInfo {
    Vec2 enemyPos; ///< Tile the attacker struck from (beam origin).
    bool ranged;   ///< true for a ranged shot, false for an adjacent melee hit.
    bool hit;      ///< true when the attack dealt damage; false for a ranged
                   ///< MISS (Feature 3). A missed ranged shot still records an
                   ///< entry so the renderer can draw the beam (dim/orange) even
                   ///< though no damage was applied. Melee attacks always hit.
};

// =============================================================================
// TurnResult — what the turn loop should do after processTurn returns.
// =============================================================================

/// The outcome of one call to TurnManager::processTurn.
///
/// The caller (Game / main loop) reads this to decide whether to start the next
/// wave, show the Game Over screen, or simply redraw and wait for the next input.
struct TurnResult {
    bool turnConsumed; ///< true if the player used their action (turn advanced).
    bool playerDied;   ///< true if the player's health reached 0 (R10.6).
    bool waveCleared;  ///< true if all enemies are gone after this turn (R10.5).
    bool quitRequested; ///< true if the player chose Quit.
    bool saveRequested; ///< true if the player chose Save.

    /// Visual-effect data for a Fire action this turn.
    ///
    /// Populated only when `cmd.type` was Fire. `fireEffect.fired` distinguishes
    /// "the player actually shot" from "Fire was rejected (e.g. no ammo)" so the
    /// caller can decide whether to show a tracer + impact flash. The trail is
    /// the cells the projectile visited (player cell first, impact cell last)
    /// and `hit` reports whether it landed on an enemy. Stays default-zero on
    /// non-Fire turns so existing callers can ignore it without harm.
    FireResult fireEffect{};

    /// Every enemy attack that struck the player during this turn's enemy phase.
    ///
    /// One entry is appended for each enemy whose decideAction resolved to an
    /// Attack. The caller (Game.cpp) walks this list after processTurn and asks
    /// the renderer to show a beam (ranged) or slash flash (melee) for each, so
    /// the player can SEE that they are being attacked (and from where). Empty
    /// on turns where no enemy attacked. Kept separate from the damage logic so
    /// the CombatSystem stays renderer-agnostic.
    std::vector<EnemyAttackInfo> enemyAttacks;

    /// Visual-effect data for a Nova ultimate fired this turn.
    ///
    /// `novaFired` is true only when the player activated Nova AND it actually
    /// fired (the Charge_Meter was full). When true, `novaCenter` is the tile
    /// the blast was centred on (the player's position) and `novaRadius` is the
    /// Chebyshev radius the blast covered, so the caller (Game.cpp) can ask the
    /// renderer to draw an expanding shockwave of the correct size — even when
    /// the blast caught no enemies, so the ultimate is always visibly fired.
    /// Stays default-false on every other turn so callers can ignore it.
    bool novaFired = false;  ///< Did a Nova blast fire this turn?
    Vec2 novaCenter{0, 0};   ///< Blast centre (the player's tile).
    int  novaRadius = 0;     ///< Blast radius in Chebyshev tiles.

    /// Visual-effect data for a player melee attack this turn (Fix 4).
    ///
    /// `playerMeleed` is true when the player walked into an enemy tile (the
    /// melee branch of Phase 1). `playerMeleeTarget` is the cell the attacked
    /// enemy occupied, so the renderer can draw a brief red slash / X over it
    /// as visual feedback.
    bool playerMeleed = false;   ///< Did the player melee-attack this turn?
    Vec2 playerMeleeTarget{0, 0}; ///< The cell the melee attack landed on.

    /// Audio-cue data for a pickup this turn.
    ///
    /// `itemPickedUp` is set to true when the player walked onto an item tile
    /// and the pickup branch in TurnManager applied the item's effect. Game.cpp
    /// reads this and asks the renderer to play the pickup chime. Stays false
    /// on every other turn so callers can ignore it without harm.
    bool itemPickedUp = false;

    /// Audio-cue data for an ability activation this turn.
    ///
    /// `abilityActivated` is set to true ONLY when the player issued a UseAbility
    /// command AND the AbilitySystem actually fired the ability (not rejected by
    /// cooldown / charge / lookup failure). `abilityKind` reports which one, so
    /// Game.cpp can dispatch to the renderer's per-ability sound. Stays at its
    /// default for non-ability turns.
    bool abilityActivated = false;
    AbilityKind abilityKind = AbilityKind::Dash;
};

// =============================================================================
// TurnManager
// =============================================================================

/// Orchestrates one complete game turn in the fixed order R10.1 specifies.
///
/// TurnManager is a stateless service: it holds no member data and all
/// mutation happens on the GameState it receives. This makes it easy to test
/// and reason about.
class TurnManager {
public:
    /// Default constructor; the class holds no data members.
    TurnManager() = default;

    /// Execute one full turn from the given player command.
    ///
    /// The five phases are run unconditionally in this order (R10.1):
    ///   1. Player action (from `cmd`).
    ///   2. Enemy actions (each enemy decideAction + execute).
    ///   3. Death resolution (removes 0-HP entities, updates kill count and
    ///      Charge_Meter, detects player death).
    ///   4. End-of-turn: tick ability cooldowns and shield via `abilities`.
    ///   5. Increment turnCount (R10.4) and check for wave clearance (R10.5).
    ///
    /// If the player command is Quit or Save no phases execute; the
    /// corresponding flag in TurnResult is set and the function returns early.
    ///
    /// If the player command is a blocked move (Wall or out-of-bounds), the
    /// turn is NOT consumed and the function returns with turnConsumed = false
    /// so the renderer can ask for another input (R11.2).
    ///
    /// @param state     the authoritative game state (modified in place).
    /// @param cmd       the action the player chose this turn.
    /// @param combat    the stateless combat service (damage, death, projectile).
    /// @param abilities the stateless ability service (activate, tick).
    /// @param pathfinder the BFS pathfinder for enemy AI.
    /// @param config    balancing configuration (Charge_Meter max, etc.).
    /// @param log       the EventLog to receive all event messages.
    /// @return a TurnResult describing the outcome (player alive/dead, wave
    ///         cleared, turn consumed, quit/save requested).
    TurnResult processTurn(GameState& state,
                           const InputCommand& cmd,
                           CombatSystem& combat,
                           AbilitySystem& abilities,
                           const Pathfinder& pathfinder,
                           const Config& config,
                           EventLog& log) const;
};

} // namespace dga
