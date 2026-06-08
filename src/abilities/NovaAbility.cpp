// =============================================================================
// abilities/NovaAbility.cpp
//
// Purpose:
//   Definitions for NovaAbility (declared in abilities/NovaAbility.h). Includes
//   the full "core/Config.h" because the constructor reads the Nova cooldown
//   duration from it.
//
// Layer: abilities (depends on abilities/Ability, core/Config).
// =============================================================================
#include "abilities/NovaAbility.h"

#include "core/Config.h" // full type so the ctor can read the Nova cooldown.

namespace dga {

// Build the base with the Nova kind tag and the Nova cooldown duration from
// Config (R8.5). That duration is 0 on purpose: Nova is gated by the
// Charge_Meter, not a turn cooldown (R22), so it carries no cooldown of its own
// and the charge requirement is enforced separately by the AbilitySystem.
NovaAbility::NovaAbility(const Config& config)
    : Ability(AbilityKind::Nova,
              config.abilityCooldownDuration(AbilityKind::Nova)) {}

// PLACEHOLDER (task 8.1): the real effect damages every enemy adjacent to the
// hero and then resets the Charge_Meter to empty (R22.2). That needs the
// GameState (hero position + enemy list), which this parameterless signature
// does not yet carry; the AbilitySystem wires it in 8.1. Leaving the effect
// empty here is safe - the base cooldown bookkeeping is already functional.
void NovaAbility::activate() {
    // Intentionally empty until task 8.1 supplies the game context.
}

} // namespace dga
