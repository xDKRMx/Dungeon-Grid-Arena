// =============================================================================
// abilities/DashAbility.h
//
// Purpose:
//   DashAbility is a concrete Ability that moves the hero several tiles in a
//   direction along open floor in a single action (R21.5). It derives from
//   Ability for its cooldown machinery and overrides activate() with the dash
//   effect (filled in by task 8.1).
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

class Config; // Forward decl: the ctor reads cooldown + dash distance from Config.

/// A movement ability that slides the hero up to a configured distance (R21.5).
class DashAbility : public Ability {
public:
    /// Construct the dash, taking its cooldown length and travel distance from
    /// Config so no magic numbers appear here (R8.5). The base is tagged with
    /// AbilityKind::Dash and the Dash cooldown duration (R21.4, R21.5).
    /// @param config the balancing configuration to read tuning from (read only
    ///        during construction; not stored).
    explicit DashAbility(const Config& config);

    /// Perform the dash (R21.5). Placeholder for now: the real bounded,
    /// wall-respecting floor move is wired in task 8.1 once the ability can see
    /// the GameState's map and the hero's position. The cooldown wiring around
    /// this call is already complete.
    void activate() override;

    /// @return how many tiles the dash travels (from Config, R21.5).
    int distance() const;

private:
    int distance_; ///< Maximum tiles moved per dash, read from Config (R21.5).
};

} // namespace dga
