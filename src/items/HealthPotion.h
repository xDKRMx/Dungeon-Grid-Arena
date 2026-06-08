// =============================================================================
// items/HealthPotion.h
//
// Purpose:
//   HealthPotion is a concrete Item that restores the Player's Health up to the
//   maximum when picked up (R19.1). It is consumable: once drunk it is removed
//   from the Inventory (R20.3).
//
//   This is one of the five Item subtypes that together demonstrate the
//   inheritance + polymorphism rubric criterion (R4.3): it overrides applyTo()
//   so that "use this item" dispatches to the heal effect through an Item base
//   reference, with no type switch anywhere (R19.6).
//
// Layer: items (depends on items/Item and, in the .cpp, entities/Player).
// =============================================================================
#pragma once

#include "items/Item.h" // Item - the abstract base this overrides.

namespace dga {

class Player; // Forward decl: applyTo() takes a Player& (resolved in the .cpp).

/// A consumable potion that heals the Player to full Health (R19.1).
class HealthPotion : public Item {
public:
    /// Construct a health potion at a grid position.
    /// @param position the grid cell the potion lies on until picked up.
    explicit HealthPotion(const Vec2& position);

    /// Heal the player up to their maximum Health (R19.1).
    /// Implemented by healing for the player's full maxHealth, which Entity::heal
    /// clamps to the ceiling - so the result is always "topped up", never over.
    /// @param player the hero to heal (modified in place).
    void applyTo(Player& player) override;

    /// @return true: a potion is used up once drunk and should leave the
    ///         Inventory (R20.3).
    bool isConsumable() const override;

    /// @return '!', the ASCII icon for a potion.
    char glyph() const override;
};

} // namespace dga
