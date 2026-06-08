// =============================================================================
// abilities/NovaAbility.h
//
// Purpose:
//   NovaAbility is the hero's ultimate: a concrete Ability that damages every
//   enemy adjacent to the hero and then consumes the full Charge_Meter (R22.2).
//   It derives from Ability and overrides activate() with the Nova effect
//   (filled in by task 8.1).
//
//   Nova is special among the four abilities: it is NOT gated by a turn
//   cooldown but by the Charge_Meter (R22). Its cooldown duration is therefore
//   configured as 0 (see Config), which keeps it "ready" by the base's cooldown
//   measure; the AbilitySystem layers the separate "charge must be full" check
//   on top (R22.3). One of the four concrete abilities demonstrating
//   inheritance + polymorphism (R4.4).
//
// Layer: abilities (depends on abilities/Ability; reads tuning from core/Config
//   in the .cpp).
// =============================================================================
#pragma once

#include "abilities/Ability.h" // Ability - the abstract base this overrides.

namespace dga {

class Config; // Forward decl: the ctor reads the Nova cooldown from Config.

/// The ultimate ability: adjacent area damage that consumes the charge (R22.2).
class NovaAbility : public Ability {
public:
    /// Construct Nova with its cooldown duration taken from Config (R8.5). That
    /// duration is 0 by design because Nova is gated by the Charge_Meter rather
    /// than a turn cooldown (R22); the base is tagged AbilityKind::Nova.
    /// @param config the balancing configuration to read tuning from (read only
    ///        during construction; not stored).
    explicit NovaAbility(const Config& config);

    /// Perform the Nova blast (R22.2). Placeholder for now: the real adjacent
    /// area-of-effect damage and the Charge_Meter reset are wired in task 8.1
    /// once the ability can see the GameState (hero position + enemies). The
    /// base cooldown wiring is already complete (and trivially "ready" since
    /// Nova's cooldown is 0; the charge gate lives in the AbilitySystem).
    void activate() override;
};

} // namespace dga
