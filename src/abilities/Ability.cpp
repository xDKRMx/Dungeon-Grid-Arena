// =============================================================================
// abilities/Ability.cpp
//
// Purpose:
//   Definitions for the Ability abstract base declared in abilities/Ability.h.
//   Only the shared cooldown machinery lives here: the constructor, the
//   defaulted virtual destructor, the trivial accessors, and the two cooldown
//   transitions (tick / putOnCooldown). The per-ability effects live in the
//   concrete subtypes (DashAbility.cpp, NovaAbility.cpp, ...).
//
// Layer: abilities (this base depends only on core/Enums.h).
// =============================================================================
#include "abilities/Ability.h"

namespace dga {

// Store the kind tag and the cooldown length, and start the ability READY: a
// freshly constructed ability has no Turns remaining on its cooldown, so the
// hero can use it immediately (R21.2).
Ability::Ability(AbilityKind kind, int cooldownDuration)
    : kind_(kind),
      cooldownRemaining_(0),
      cooldownDuration_(cooldownDuration) {}

// Defaulted out-of-line to anchor the vtable and allow safe deletion through an
// Ability* base pointer (R4.7).
Ability::~Ability() = default;

AbilityKind Ability::kind() const {
    return kind_;
}

// "Ready" is exactly "no cooldown Turns remain" (R21.2).
bool Ability::isReady() const {
    return cooldownRemaining_ == 0;
}

int Ability::cooldownRemaining() const {
    return cooldownRemaining_;
}

int Ability::cooldownDuration() const {
    return cooldownDuration_;
}

// Count one Turn off the cooldown, clamping at 0 so a ready ability stays ready
// (R21.4). The explicit guard keeps the intent obvious: the remaining count
// never goes negative.
void Ability::tick() {
    if (cooldownRemaining_ > 0) {
        cooldownRemaining_ -= 1;
    }
}

// Lock the ability for its full configured duration after a successful use
// (R21.4). From here tick() walks the remaining count back down to 0.
void Ability::putOnCooldown() {
    cooldownRemaining_ = cooldownDuration_;
}

// Force the remaining cooldown back to 0. Used by the Blink Chain buff
// (Feature 2): after a normal putOnCooldown() the AbilitySystem calls this
// to make Blink immediately ready again while charges remain.
void Ability::clearCooldown() {
    cooldownRemaining_ = 0;
}

} // namespace dga
