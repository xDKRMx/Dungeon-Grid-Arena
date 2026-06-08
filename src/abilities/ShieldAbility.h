// =============================================================================
// abilities/ShieldAbility.h
//
// Purpose:
//   ShieldAbility is a concrete Ability that grants the hero damage immunity for
//   a configured number of Turns (R21.6). It derives from Ability for its
//   cooldown machinery and overrides activate() with the shield effect (filled
//   in by task 8.1).
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

class Config; // Forward decl: the ctor reads cooldown + shield duration from Config.

/// A defensive ability granting temporary damage immunity (R21.6).
class ShieldAbility : public Ability {
public:
    /// Construct the shield, taking its cooldown length and immunity duration
    /// from Config so no magic numbers appear here (R8.5). The base is tagged
    /// AbilityKind::Shield with the Shield cooldown duration (R21.4, R21.6).
    /// @param config the balancing configuration to read tuning from (read only
    ///        during construction; not stored).
    explicit ShieldAbility(const Config& config);

    /// Raise the shield (R21.6). Placeholder for now: the real timed damage
    /// immunity is wired in task 8.1 once the ability can flag the hero as
    /// immune for durationTurns() Turns. The cooldown wiring around this call is
    /// already complete.
    void activate() override;

    /// @return how many Turns the shield grants immunity (from Config, R21.6).
    int durationTurns() const;

private:
    int durationTurns_; ///< Turns of damage immunity granted, from Config (R21.6).
};

} // namespace dga
