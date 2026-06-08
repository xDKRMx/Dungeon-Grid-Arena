// =============================================================================
// items/Item.h
//
// Purpose:
//   Item is the abstract base class for every collectible that can lie on the
//   dungeon floor and be picked up by the Player: Health_Potion, Weapon,
//   Ammo_Item, Armor, and Treasure (R4.3). It is the project's second big
//   inheritance/polymorphism showcase: each concrete item overrides applyTo()
//   so that "use this item on the player" dispatches to the right effect through
//   a single Item base reference, with no switch on item kind (R19.6).
//
//   The base stores only what ALL items share: where the item sits on the grid
//   and which concrete kind it is (for save/load tags and HUD icons). The actual
//   effect lives entirely in the subclasses.
//
// The dependency knot (resolved with a forward declaration):
//   applyTo() needs to modify a Player, but Player (in the entities layer) in
//   turn owns an Inventory of Item* and an equipped Weapon*. To avoid a circular
//   #include, this header only *forward-declares* Player; the concrete item
//   .cpp files include the full "entities/Player.h" where they actually call
//   Player's members.
//
// Why a .h/.cpp split:
//   Item has real (if small) shared logic and a virtual destructor, so its
//   declarations live here and its definitions live in Item.cpp (R2.1, R4.7).
//
// Layer: items (depends on core; uses Player from entities only by reference).
// =============================================================================
#pragma once

#include "core/Enums.h" // ItemKind - tags which concrete item this is.
#include "core/Vec2.h"  // Vec2 - the item's grid position.

namespace dga {

class Player; // Forward declaration: applyTo() takes a Player& (see file header).

/// Abstract base for any collectible Item on the dungeon floor (R4.3).
///
/// Item is abstract because applyTo() and glyph() are pure virtual: you can
/// never construct a bare Item, only one of the concrete subtypes. Shared state
/// is `protected` so subclasses can read it while outside code uses the public
/// accessors (R1.2).
class Item {
public:
    /// Construct an item of a given kind at a grid position.
    /// @param kind     which concrete kind of item this is (HealthPotion, ...).
    /// @param position the grid cell the item lies on until it is picked up.
    Item(ItemKind kind, const Vec2& position);

    /// Virtual destructor so an Item subtype can be safely deleted through an
    /// `Item*` (the GameState owns items as `unique_ptr<Item>`) (R4.7).
    virtual ~Item();

    /// Apply this item's effect to the Player who picked it up (R19.6).
    /// Pure virtual: each concrete item supplies its own effect (heal, equip,
    /// add ammo, raise armor, ...), and the call dispatches by the item's real
    /// type through virtual dispatch.
    /// @param player the Player to apply the effect to (modified in place).
    virtual void applyTo(Player& player) = 0;

    /// Report whether the item is used up when applied.
    /// @return true for one-shot items that should be removed from the Inventory
    ///         after use (potions, ammo); false for persistent items (weapons,
    ///         armor) that keep mattering after pickup (R20.3). The base returns
    ///         false; consumable subtypes override to return true.
    virtual bool isConsumable() const;

    /// The ASCII symbol used to draw this item in the console renderer.
    /// Pure virtual so every concrete item chooses its own icon.
    /// @return the single character that represents this item on screen.
    virtual char glyph() const = 0;

    /// @return which concrete kind of item this is (R4 polymorphism tag).
    ItemKind kind() const;

    /// @return the grid cell the item currently lies on.
    Vec2 position() const;

    /// Move the item to a new grid cell (used when placing items during
    /// generation).
    /// @param newPosition the grid coordinate to place the item on.
    void setPosition(const Vec2& newPosition);

protected:
    ItemKind kind_;  ///< Category tag for this concrete item (R4).
    Vec2 position_;  ///< Grid cell the item occupies until picked up.
};

} // namespace dga
