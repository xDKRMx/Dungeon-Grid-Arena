// =============================================================================
// items/Inventory.cpp
//
// Purpose:
//   Definitions for the Inventory class declared in items/Inventory.h. The only
//   real logic here is remove(), which finds an item by pointer identity and
//   erases just that reference - crucially without deleting the underlying Item,
//   because the Inventory does not own its items (the GameState does).
//
//   No "items/Item.h" include is needed: every operation works on the Item*
//   pointer itself, never on Item's members, so the forward declaration in the
//   header suffices and the build stays free of the Item/Player/Inventory cycle.
//
// Layer: items (depends on core; references Item by pointer only).
// =============================================================================
#include "items/Inventory.h"

#include <algorithm> // std::remove - shifts the matching pointer to the end.

namespace dga {

// Append a held item. Null is rejected so the contents list can never contain a
// dangling/never-valid entry that the HUD would try to display.
void Inventory::add(Item* item) {
    if (item == nullptr) {
        return;
    }
    contents_.push_back(item);
}

// Drop a reference to an item by pointer identity. std::remove gathers every
// matching pointer at the end of the vector and returns the new logical end;
// erase() then trims them off. We compare pointers (not item contents) because
// the inventory tracks specific item instances. The Item object itself is left
// untouched - deleting it is the GameState's job, never the Inventory's (R20.3).
void Inventory::remove(Item* item) {
    if (item == nullptr) {
        return;
    }
    contents_.erase(
        std::remove(contents_.begin(), contents_.end(), item),
        contents_.end());
}

// Read-only view of the held items for the HUD (R20.2). Returning a const
// reference avoids copying the vector while preventing outside mutation.
const std::vector<Item*>& Inventory::contents() const {
    return contents_;
}

} // namespace dga
