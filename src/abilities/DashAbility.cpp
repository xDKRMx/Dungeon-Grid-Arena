// =============================================================================
// abilities/DashAbility.cpp
//
// Purpose:
//   Definitions for DashAbility (declared in abilities/DashAbility.h). Includes
//   the full "core/Config.h" because the constructor reads the Dash cooldown
//   duration and travel distance from it.
//
// Layer: abilities (depends on abilities/Ability, core/Config).
// =============================================================================
#include "abilities/DashAbility.h"

#include "core/Config.h" // full type so the ctor can read cooldown + distance.

namespace dga {

// Build the base with the Dash kind tag and the Dash cooldown duration from
// Config (R21.4), then cache the dash distance, also from Config (R21.5). No
// literals appear here - every tunable comes from the single config source.
DashAbility::DashAbility(const Config& config)
    : Ability(AbilityKind::Dash,
              config.abilityCooldownDuration(AbilityKind::Dash)),
      distance_(config.dashDistance()) {}

// PLACEHOLDER (task 8.1): the real effect moves the hero up to distance_ tiles
// along open floor in the chosen direction, stopping at the first wall, map
// edge, or occupied tile (R21.5). That needs the GameState (map + hero
// position), which this parameterless signature does not yet carry; the
// AbilitySystem wires it in 8.1. The cooldown bookkeeping in the base is already
// fully functional, so leaving the effect empty here is safe and intentional.
void DashAbility::activate() {
    // Intentionally empty until task 8.1 supplies the game context.
}

int DashAbility::distance() const {
    return distance_;
}

} // namespace dga
