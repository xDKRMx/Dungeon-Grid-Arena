// =============================================================================
// items/Armor.cpp
//
// Purpose:
//   Definitions for Armor (declared in items/Armor.h). Includes the full
//   "entities/Player.h" because applyTo() calls Player::addArmor().
//
// Layer: items (depends on items/Item, entities/Player).
// =============================================================================
#include "items/Armor.h"

#include "entities/Player.h" // full Player type so we can call addArmor().

namespace dga {

// Tag the base as Armor, place it on the grid, and store the protection bonus.
Armor::Armor(const Vec2& position, int armorBonus)
    : Item(ItemKind::Armor, position),
      armorBonus_(armorBonus) {}

// Increase the hero's armor by this pickup's bonus (R19.4). addArmor ignores
// negative values, so a malformed pickup can never weaken the hero's defense.
void Armor::applyTo(Player& player) {
    player.addArmor(armorBonus_);
}

// Armor persists after pickup: the bonus is permanently part of the hero's
// armor stat, so there is nothing to "use up" (mirrors Weapon, not potions).
bool Armor::isConsumable() const {
    return false;
}

char Armor::glyph() const {
    return ']';
}

int Armor::armorBonus() const {
    return armorBonus_;
}

} // namespace dga
