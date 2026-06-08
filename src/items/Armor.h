// =============================================================================
// items/Armor.h
//
// Purpose:
//   Armor is a concrete Item that, when picked up, raises the Player's armor
//   (damage reduction) value (R19.4). Like a Weapon it is NOT consumable: the
//   bonus it grants is folded permanently into the hero's armor stat, so the
//   item is kept as a record of equipped protection rather than being used up
//   and discarded the way a potion is.
//
//   It is one of the five Item subtypes that together demonstrate the
//   inheritance + polymorphism rubric criterion (R4.3): it overrides applyTo()
//   so that "use this item" dispatches to the raise-armor effect through an Item
//   base reference, with no type switch anywhere (R19.6).
//
// Layer: items (depends on items/Item and, in the .cpp, entities/Player).
// =============================================================================
#pragma once

#include "items/Item.h" // Item - the abstract base this overrides.

namespace dga {

class Player; // Forward decl: applyTo() takes a Player& (resolved in the .cpp).

/// A persistent armor pickup that increases the Player's damage reduction (R19.4).
class Armor : public Item {
public:
    /// Construct an armor pickup at a grid position with its protection value.
    /// @param position   the grid cell the armor lies on until picked up.
    /// @param armorBonus how much to add to the hero's armor (damage reduction)
    ///        when this pickup is applied (R19.4).
    Armor(const Vec2& position, int armorBonus);

    /// Raise the player's armor by this pickup's bonus (R19.4). The actual add
    /// (and the rejection of nonsensical negative deltas) lives in
    /// Player::addArmor, so this method just forwards the configured bonus.
    /// @param player the hero gaining the protection (modified in place).
    void applyTo(Player& player) override;

    /// @return false: the armor bonus is permanently applied to the hero, so the
    ///         item persists after pickup rather than being consumed (matches
    ///         the Weapon model; contrast HealthPotion/AmmoItem) (R20.3).
    bool isConsumable() const override;

    /// @return ']', the ASCII icon for a piece of armor.
    char glyph() const override;

    /// @return the armor (damage reduction) bonus this pickup grants.
    int armorBonus() const;

private:
    int armorBonus_; ///< Damage reduction added to the hero on pickup (R19.4).
};

} // namespace dga
