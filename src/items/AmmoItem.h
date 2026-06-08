// =============================================================================
// items/AmmoItem.h
//
// Purpose:
//   AmmoItem is a concrete Item that increases the Player's ammunition reserve
//   when picked up (R19.3, OD-3 limited ammo). It is consumable: once collected
//   the rounds are added and the pickup leaves the Inventory (R20.3).
//
//   One of the five Item subtypes demonstrating inheritance + polymorphism
//   (R4.3): applyTo() is overridden to add ammo via virtual dispatch (R19.6).
//
// Layer: items (depends on items/Item and, in the .cpp, entities/Player).
// =============================================================================
#pragma once

#include "items/Item.h" // Item - the abstract base this overrides.

namespace dga {

class Player; // Forward decl: applyTo() takes a Player& (resolved in the .cpp).

/// A consumable ammo pickup that refills the Player's ammunition (R19.3).
class AmmoItem : public Item {
public:
    /// Construct an ammo pickup at a grid position.
    /// @param position the grid cell the pickup lies on until collected.
    /// @param amount   how many rounds this pickup grants (R19.3).
    AmmoItem(const Vec2& position, int amount);

    /// Add this pickup's rounds to the player's ammo reserve (R19.3).
    /// @param player the hero collecting the ammo (modified in place).
    void applyTo(Player& player) override;

    /// @return true: an ammo pickup is consumed once collected (R20.3).
    bool isConsumable() const override;

    /// @return '=', the ASCII icon for ammunition.
    char glyph() const override;

    /// @return the number of rounds this pickup grants.
    int amount() const;

private:
    int amount_; ///< Rounds added to the player's reserve on pickup (R19.3).
};

} // namespace dga
