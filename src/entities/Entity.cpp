// =============================================================================
// entities/Entity.cpp
//
// Purpose:
//   Definitions for the Entity abstract base declared in entities/Entity.h.
//   The interesting logic here is the Health clamping in takeDamage()/heal():
//   both keep current Health inside the closed range [0, maxHealth] so combat
//   can never drive Health negative or heal an actor past its ceiling (R15.1,
//   R15.3, R19.1). Everything else is a trivial accessor.
//
// Layer: entities (depends on core/Vec2.h and core/Enums.h only).
// =============================================================================
#include "entities/Entity.h"

namespace dga {

// Initialize every shared stat from the constructor arguments. Current Health
// starts equal to the supplied starting Health; the caller is responsible for
// passing sensible values (e.g. health <= maxHealth) from Config (R15.1).
Entity::Entity(EntityKind kind, const Vec2& position, int health, int maxHealth,
               int attack, int armor)
    : position_(position),
      health_(health),
      maxHealth_(maxHealth),
      attack_(attack),
      armor_(armor),
      kind_(kind) {}

// Defaulted out-of-line so the class has a key function and a single place that
// anchors its vtable. Declared virtual in the header so deleting a Player or
// Enemy through an Entity* destroys the full object correctly (R4.7).
Entity::~Entity() = default;

// "Alive" is strictly Health > 0, matching the rule that an entity dies the
// moment its Health reaches 0 (R15.3).
bool Entity::isAlive() const {
    return health_ > 0;
}

// Subtract damage and clamp the low end. We guard the negative case explicitly
// instead of using a library helper to keep the intent obvious for a beginner
// reader: Health can never fall below 0 (R15.3).
void Entity::takeDamage(int amount) {
    health_ -= amount;
    if (health_ < 0) {
        health_ = 0;
    }
}

// Add Health and clamp the high end so a heal can never exceed maxHealth, which
// is exactly the "up to the maximum" rule for health pickups (R19.1).
void Entity::heal(int amount) {
    health_ += amount;
    if (health_ > maxHealth_) {
        health_ = maxHealth_;
    }
}

// ---- Simple read-only accessors --------------------------------------------
// Each is const because it does not modify the entity (R1.3).

Vec2 Entity::position() const {
    return position_;
}

void Entity::setPosition(const Vec2& newPosition) {
    position_ = newPosition;
}

int Entity::health() const {
    return health_;
}

int Entity::maxHealth() const {
    return maxHealth_;
}

int Entity::attack() const {
    return attack_;
}

int Entity::armor() const {
    return armor_;
}

// Subtract armor points and clamp at zero so armor can never go negative.
// CombatSystem calls this when incoming damage is absorbed by the armor buffer
// before reaching Health (armor-as-shield-buffer mechanic).
void Entity::reduceArmor(int amount) {
    armor_ -= amount;
    if (armor_ < 0) {
        armor_ = 0;
    }
}

// Permanently grow both the Health cap and the current Health by `amount`.
// Used by WaveManager to scale enemy survivability with the wave number so
// the player's mid-run damage upgrades stay balanced. We bump current Health
// by the same delta so a freshly-spawned boosted enemy is at full boosted
// Health, not starting "wounded" at the old maximum.
void Entity::boostMaxHealth(int amount) {
    if (amount <= 0) {
        return; // Defensive: ignore non-positive deltas (no shrinking allowed).
    }
    maxHealth_ += amount;
    health_    += amount;
    // Defensive clamp in case a future caller passes a value that combined
    // with floor()/ceil()-flavoured math in WaveManager would otherwise push
    // health_ above maxHealth_; keeps the invariant 0 <= health_ <= maxHealth_.
    if (health_ > maxHealth_) {
        health_ = maxHealth_;
    }
}

EntityKind Entity::kind() const {
    return kind_;
}

} // namespace dga
