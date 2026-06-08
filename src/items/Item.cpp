// =============================================================================
// items/Item.cpp
//
// Purpose:
//   Definitions for the Item abstract base declared in items/Item.h. Only the
//   shared, non-virtual parts live here: the constructor, the defaulted virtual
//   destructor, the default isConsumable() answer, and the trivial accessors.
//   The per-item effects live in the concrete subtypes (HealthPotion.cpp, etc.).
//
//   Note that this file does NOT need to include "entities/Player.h": the base
//   class never touches Player itself (only the subclasses' applyTo() do), so a
//   forward declaration in the header is enough here.
//
// Layer: items (depends on core only).
// =============================================================================
#include "items/Item.h"

namespace dga {

// Store the kind tag and starting position; the effect data is added by
// subclasses through their own constructors.
Item::Item(ItemKind kind, const Vec2& position)
    : kind_(kind),
      position_(position) {}

// Defaulted out-of-line to anchor the vtable and allow safe deletion through an
// Item* base pointer (R4.7).
Item::~Item() = default;

// Sensible default: most items persist after pickup. Consumable subtypes
// (HealthPotion, AmmoItem) override this to return true so the Inventory knows
// to remove them once applied (R20.3).
bool Item::isConsumable() const {
    return false;
}

ItemKind Item::kind() const {
    return kind_;
}

Vec2 Item::position() const {
    return position_;
}

void Item::setPosition(const Vec2& newPosition) {
    position_ = newPosition;
}

} // namespace dga
