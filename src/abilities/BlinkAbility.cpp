// =============================================================================
// abilities/BlinkAbility.cpp
//
// Purpose:
//   Definitions for BlinkAbility (declared in abilities/BlinkAbility.h).
//   Includes the full "core/Config.h" because the constructor reads the Blink
//   cooldown duration from it.
//
// Layer: abilities (depends on abilities/Ability, core/Config).
// =============================================================================
#include "abilities/BlinkAbility.h"

#include "core/Config.h" // full type so the ctor can read the Blink cooldown.

namespace dga {

// Build the base with the Blink kind tag and the Blink cooldown duration from
// Config (R21.4, R21.7). No literals here - the cooldown comes from the single
// config source.
BlinkAbility::BlinkAbility(const Config& config)
    : Ability(AbilityKind::Blink,
              config.abilityCooldownDuration(AbilityKind::Blink)) {}

// PLACEHOLDER (task 8.1): the real effect teleports the hero onto a visible,
// empty, walkable floor tile (R21.7). That needs the GameState (map, line of
// sight, occupancy), which this parameterless signature does not yet carry; the
// AbilitySystem wires it in 8.1. Leaving the effect empty here is safe - the
// base cooldown bookkeeping is already functional.
void BlinkAbility::activate() {
    // Intentionally empty until task 8.1 supplies the game context.
}

} // namespace dga
