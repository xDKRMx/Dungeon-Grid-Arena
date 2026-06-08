// =============================================================================
// items/Inventory.h
//
// Purpose:
//   Inventory is the Player's collection of held Items (R20). It records WHICH
//   items the player is currently carrying so the HUD can list them (R20.2) and
//   so consumables can be removed when used (R20.3).
//
// Ownership (important):
//   The Inventory stores NON-OWNING raw pointers (`Item*`). The authoritative
//   owner of every Item object is the GameState (it holds them in a
//   `std::vector<std::unique_ptr<Item>>`). The Inventory merely *references*
//   items that also live in the GameState. Therefore the Inventory NEVER deletes
//   an item: add()/remove() only insert/erase pointers from its list, leaving
//   the lifetime of the pointed-to objects entirely to the GameState. Using raw
//   pointers here (rather than unique_ptr) is deliberate and correct: two owners
//   of the same object would be a double-delete bug.
//
// The dependency knot (resolved with a forward declaration):
//   Inventory needs to name Item but not its full definition (it only stores
//   pointers and references), so this header forward-declares Item and the .cpp
//   includes "items/Item.h" only if it needs the full type. This keeps the
//   Inventory header light and breaks the Item <-> Player <-> Inventory cycle.
//
// Why a .h/.cpp split:
//   Inventory owns real container logic (search-and-erase on remove), so its
//   declarations live here and its definitions live in Inventory.cpp (R2.1).
//
// Layer: items (depends on core; references Item by pointer only).
// =============================================================================
#pragma once

#include <vector> // std::vector - the backing list of item pointers.

namespace dga {

class Item; // Forward declaration: Inventory only stores Item* (see file header).

/// The Player's bag of currently held items, stored as non-owning pointers.
///
/// The Inventory does not own the Item objects it lists; it only tracks which
/// items (owned elsewhere by the GameState) the player is holding (R20.1). It
/// therefore never frees an item.
class Inventory {
public:
    /// Add an item to the inventory.
    /// @param item a pointer to the item being picked up; ownership is NOT
    ///        transferred (the GameState still owns it). Null pointers are
    ///        ignored so a bad caller can never store a dangling entry.
    void add(Item* item);

    /// Remove a specific item from the inventory, if present.
    /// @param item the pointer to remove; the matching entry (by pointer
    ///        identity) is erased. The pointed-to Item is NOT deleted - only the
    ///        reference is dropped (R20.3). Absent or null pointers are no-ops.
    void remove(Item* item);

    /// Expose the current contents for display in the HUD (R20.2).
    /// @return a const reference to the internal vector of item pointers. The
    ///         reference is read-only so callers can iterate the held items but
    ///         cannot mutate the inventory behind its back.
    const std::vector<Item*>& contents() const;

private:
    std::vector<Item*> contents_; ///< Non-owning pointers to held items (R20.1).
};

} // namespace dga
