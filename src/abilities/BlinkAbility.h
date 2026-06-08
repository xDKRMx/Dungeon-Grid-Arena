// =============================================================================
// abilities/BlinkAbility.h
//
// Purpose:
//   BlinkAbility is a concrete Ability that teleports the hero to a visible,
//   empty floor tile (R21.7). It derives from Ability for its cooldown
//   machinery and overrides activate() with the blink effect (filled in by
//   task 8.1).
//
//   One of the four concrete abilities demonstrating inheritance + polymorphism
//   (R4.4).
//
// Layer: abilities (depends on abilities/Ability; reads tuning from core/Config
//   in the .cpp).
// =============================================================================
#pragma once

#include "abilities/Ability.h" // Ability - the abstract base this overrides.

namespace dga {

class Config; // Forward decl: the ctor reads the Blink cooldown from Config.

/// A mobility ability that teleports the hero to a visible empty floor (R21.7).
class BlinkAbility : public Ability {
public:
    /// Construct the blink with its cooldown duration taken from Config so no
    /// magic numbers appear here (R8.5). The base is tagged AbilityKind::Blink
    /// with the Blink cooldown duration (R21.4, R21.7).
    /// @param config the balancing configuration to read tuning from (read only
    ///        during construction; not stored).
    explicit BlinkAbility(const Config& config);

    /// Perform the blink (R21.7). Placeholder for now: the real teleport to a
    /// visible, empty, walkable floor tile is wired in task 8.1 once the ability
    /// can see the GameState (map + line of sight + occupancy). The cooldown
    /// wiring around this call is already complete.
    void activate() override;
};

} // namespace dga
