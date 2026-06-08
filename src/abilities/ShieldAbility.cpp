// =============================================================================
// abilities/ShieldAbility.cpp
//
// Purpose:
//   Definitions for ShieldAbility (declared in abilities/ShieldAbility.h).
//   Includes the full "core/Config.h" because the constructor reads the Shield
//   cooldown duration and immunity duration from it.
//
// Layer: abilities (depends on abilities/Ability, core/Config).
// =============================================================================
#include "abilities/ShieldAbility.h"

#include "core/Config.h" // full type so the ctor can read cooldown + duration.

namespace dga {

// Build the base with the Shield kind tag and the Shield cooldown duration from
// Config (R21.4), then cache the immunity duration, also from Config (R21.6).
// Every tunable comes from the single config source - no literals here.
ShieldAbility::ShieldAbility(const Config& config)
    : Ability(AbilityKind::Shield,
              config.abilityCooldownDuration(AbilityKind::Shield)),
      durationTurns_(config.shieldDurationTurns()) {}

// PLACEHOLDER (task 8.1): the real effect flags the hero as immune to damage for
// durationTurns_ Turns (R21.6). That needs the GameState/hero, which this
// parameterless signature does not yet carry; the AbilitySystem wires it in 8.1.
// Leaving the effect empty here is safe - the base cooldown bookkeeping works.
void ShieldAbility::activate() {
    // Intentionally empty until task 8.1 supplies the game context.
}

int ShieldAbility::durationTurns() const {
    return durationTurns_;
}

} // namespace dga
