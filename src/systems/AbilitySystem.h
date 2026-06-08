// =============================================================================
// systems/AbilitySystem.h
//
// Purpose:
//   AbilitySystem is the stateless service that executes player ability
//   activations and manages ability cooldowns / the Charge_Meter (R21, R22).
//
//   Responsibilities:
//     * activate()       — attempt to fire an ability; rejects on cooldown or,
//                          for Nova, when the Charge_Meter is not full; otherwise
//                          carries out the ACTUAL effect directly here, calling
//                          ability.putOnCooldown() once done (R21.2, R21.3, R22.2,
//                          R22.3).
//     * tickCooldowns()  — advance every owned ability's cooldown by one Turn at
//                          end-of-turn (R21.4), and tick the player's shield.
//     * addCharge()      — increment the Charge_Meter by a given amount, clamping
//                          to the configured maximum (R22.1).
//
//   Ability effects wired here (R21.5-21.7, R22.2):
//     * DashAbility  — moves the player along floor tiles in a direction for up
//                      to distance() steps, stopping at the first wall/boundary
//                      (R21.5).
//     * ShieldAbility — calls player.applyShield(durationTurns()), granting
//                       timed damage immunity via CombatSystem's shield check
//                       (R21.6).
//     * BlinkAbility  — teleports the player to a random visible empty floor tile
//                       found with LineOfSight; falls back to no-op if none
//                       exists (R21.7).
//     * NovaAbility   — damages every enemy at Chebyshev distance == 1 from the
//                       player and then resets the Charge_Meter to 0 (R22.2).
//
//   Effect dispatch is done with dynamic_cast to the concrete ability type (or
//   equivalently on ability.kind()), so the AbilitySystem "wires" the game
//   context into what would otherwise be a no-op placeholder activate().
//
// Why a .h/.cpp split:
//   AbilitySystem has real logic; declarations live here and definitions in
//   AbilitySystem.cpp (R2.1).
//
// Layer: systems (depends on abilities, entities, world, combat, core).
// =============================================================================
#pragma once

#include "core/Enums.h" // AbilityKind - used to identify which ability to fire.
#include "core/Vec2.h"  // Vec2 - direction parameter for Dash activation.

namespace dga {

class Ability;
class Config;
class EventLog;
class GameState;
class Player;

/// Out-parameter carrier reporting any renderer-facing effect an ability
/// activation produced, so the upstream layers (TurnManager → Game) can drive a
/// transient visual for it without the AbilitySystem ever touching the renderer
/// (the systems layer stays render-agnostic, mirroring CombatSystem's
/// FireResult). Today only Nova reports through it; the fields stay default-zero
/// for every other ability so callers can ignore them harmlessly.
///
/// Field semantics:
///   * novaFired  — true when a Nova blast actually went off this activation
///                  (charge was full and the ability fired). The renderer draws
///                  the expanding shockwave only when this is true, so pressing
///                  2 with a full meter ALWAYS shows the ultimate even if no
///                  enemy was in range.
///   * novaCenter — the tile the blast was centred on (the player's position).
///   * novaRadius — the Chebyshev radius the blast covered (from Config), so the
///                  renderer can size the shockwave to match the real AoE.
struct AbilityEffectInfo {
    bool novaFired = false;  ///< Did a Nova blast fire this activation?
    Vec2 novaCenter{0, 0};   ///< Blast centre (the player's tile).
    int  novaRadius = 0;     ///< Blast radius in Chebyshev tiles.
};

/// Stateless service that activates player abilities and manages cooldowns and
/// the Charge_Meter (R21, R22). All member functions are const (no object state).
class AbilitySystem {
public:
    /// Default constructor; the class holds no data members.
    AbilitySystem() = default;

    // ---- Ability activation (R21.2, R21.3, R22.2, R22.3) -----------------

    /// Attempt to activate one of the player's abilities.
    ///
    /// Finds the ability of the given kind in player.abilities(). If not found,
    /// logs a message and returns false.
    ///
    /// Rejection conditions (R21.3, R22.3):
    ///   * ability.isReady() is false (cooldown > 0) → reject with log message.
    ///   * kind == Nova AND player.chargeMeter() < config.chargeMeterMax() →
    ///     reject with log message (R22.3).
    ///
    /// On success: executes the ACTUAL effect (see file header), calls
    /// ability.putOnCooldown(), and returns true (R21.2).
    ///
    /// @param kind      which ability kind to activate.
    /// @param direction unit Vec2 in the chosen movement direction (used by
    ///                  Dash; ignored by Shield / Blink / Nova).
    /// @param state     the authoritative game state (map, player, enemies).
    /// @param config    balancing configuration (Charge_Meter max, etc.).
    /// @param log       EventLog to receive rejection / activation messages.
    /// @param outEffect optional out-parameter; when non-null it is populated
    ///                  with any renderer-facing effect the activation produced
    ///                  (currently the Nova blast centre/radius). Left untouched
    ///                  for abilities that produce no visual, and may be null
    ///                  when the caller does not care about effects.
    /// @return true when the ability fired, false when rejected or not found.
    bool activate(AbilityKind kind,
                  const Vec2& direction,
                  GameState& state,
                  const Config& config,
                  EventLog& log,
                  AbilityEffectInfo* outEffect = nullptr) const;

    // ---- Per-turn upkeep (R21.4) ------------------------------------------

    /// Tick every ability owned by the player by one Turn (R21.4).
    ///
    /// Calls ability.tick() on each element of player.abilities(), which
    /// decrements each nonzero cooldown toward 0. Also calls player.tickShield()
    /// so the active shield duration counts down in sync with cooldowns, and
    /// player.tickFireCooldown() so the Fire-action throttle (Config::
    /// playerFireCooldown) ticks down with every other per-turn timer.
    ///
    /// @param player the hero whose ability cooldowns and shield are decremented.
    void tickCooldowns(Player& player) const;

    // ---- Charge meter management (R22.1) ----------------------------------

    /// Add `amount` to the player's Charge_Meter, clamping to `maxCharge`
    /// (R22.1). Negative amounts are treated as zero so a kill can never
    /// accidentally reduce the meter.
    ///
    /// @param player    the hero whose Charge_Meter is incremented.
    /// @param amount    the increment; negative values are ignored.
    /// @param maxCharge the upper bound the meter must not exceed (R22.1).
    void addCharge(Player& player, int amount, int maxCharge) const;

private:
    // ---- Effect helpers (called from activate) ----------------------------

    /// Apply the Dash effect: move the player along floor tiles in `direction`
    /// for up to `maxSteps` tiles, stopping at the first wall, map boundary, or
    /// occupied tile (R21.5).
    void applyDash(GameState& state,
                   const Vec2& direction,
                   int maxSteps,
                   EventLog& log) const;

    /// Apply the Shield effect: grant the player `durationTurns` Turns of damage
    /// immunity (R21.6).
    void applyShieldEffect(GameState& state,
                           int durationTurns,
                           EventLog& log) const;

    /// Apply the Blink effect: teleport the player to a random visible empty
    /// floor tile (R21.7). Does nothing (logs a message) if no valid target
    /// exists.
    void applyBlink(GameState& state, EventLog& log) const;

    /// Apply the Nova effect: deal a large radial blast to every enemy within
    /// the configured Nova radius of the player (Chebyshev distance <=
    /// config.novaRadius()), using a simple two-tier falloff (full novaDamage in
    /// the inner half-radius, half novaDamage in the outer ring), ignoring enemy
    /// armor; then reset the Charge_Meter to 0 (R22.2). Populates `outEffect`
    /// (when non-null) with the blast centre and radius so the renderer can draw
    /// the shockwave even when no enemy was in range.
    void applyNova(GameState& state,
                   const Config& config,
                   EventLog& log,
                   AbilityEffectInfo* outEffect) const;
};

} // namespace dga
