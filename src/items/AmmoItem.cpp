// =============================================================================
// items/AmmoItem.cpp
//
// Purpose:
//   Definitions for AmmoItem (declared in items/AmmoItem.h). Includes the full
//   "entities/Player.h" because applyTo() calls Player::addAmmo().
//
// Layer: items (depends on items/Item, entities/Player).
// =============================================================================
#include "items/AmmoItem.h"

#include "entities/Player.h" // full Player type so we can call addAmmo().

namespace dga {

// Tag the base as an AmmoItem, place it on the grid, and store the round count.
AmmoItem::AmmoItem(const Vec2& position, int amount)
    : Item(ItemKind::AmmoItem, position),
      amount_(amount) {}

// Refill the player's ammo by this pickup's amount (R19.3). addAmmo ignores
// negative values, so a malformed pickup can never reduce the reserve.
void AmmoItem::applyTo(Player& player) {
    player.addAmmo(amount_);
}

// Ammo is a one-shot pickup; true tells the Inventory to remove it (R20.3).
bool AmmoItem::isConsumable() const {
    return true;
}

char AmmoItem::glyph() const {
    return '=';
}

int AmmoItem::amount() const {
    return amount_;
}

} // namespace dga
