// =============================================================================
// items/Treasure.cpp
//
// Purpose:
//   Definitions for Treasure (declared in items/Treasure.h). Unlike the other
//   item .cpp files, applyTo() does not touch the Player, so this file still
//   includes "entities/Player.h" only to complete the override's signature
//   against the same full type the base expects; the parameter is intentionally
//   unused (treasure is scored by the systems layer via value()).
//
// Layer: items (depends on items/Item, entities/Player).
// =============================================================================
#include "items/Treasure.h"

#include "entities/Player.h" // full Player type to match applyTo(Player&)'s contract.

namespace dga {

// Tag the base as Treasure, place it on the grid, and store its score value.
Treasure::Treasure(const Vec2& position, int value)
    : Item(ItemKind::Treasure, position),
      value_(value) {}

// Deliberate no-op (R19.5): a treasure's worth is added to the run's Score,
// which is owned by the systems layer (GameState / ScoreBoard), not to any
// Player stat. The collection path reads value() and credits the Score itself,
// so there is nothing to apply to the hero here. The parameter is cast to void
// to document that ignoring it is intentional, not an oversight.
void Treasure::applyTo(Player& player) {
    (void)player;
}

// Treasure is a one-shot pickup: once collected and scored it leaves play, so
// the Inventory removes it after application (R20.3).
bool Treasure::isConsumable() const {
    return true;
}

char Treasure::glyph() const {
    return '$';
}

int Treasure::value() const {
    return value_;
}

} // namespace dga
