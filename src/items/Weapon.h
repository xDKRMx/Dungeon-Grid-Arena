// =============================================================================
// items/Weapon.h
//
// Purpose:
//   Weapon is a concrete Item that, when picked up, updates the Player's attack
//   value and ranged capability (R19.2). Unlike a potion it is NOT consumable:
//   the hero keeps holding it, and the Player stores a non-owning pointer to the
//   equipped Weapon so the combat system can read its stats (e.g. whether it is
//   ranged, for ammo-consuming fire actions, OD-3 / R16).
//
//   It is one of the five Item subtypes demonstrating inheritance + polymorphism
//   (R4.3): applyTo() is overridden to equip the weapon via virtual dispatch
//   (R19.6).
//
// Layer: items (depends on items/Item and, in the .cpp, entities/Player).
// =============================================================================
#pragma once

#include "items/Item.h" // Item - the abstract base this overrides.

namespace dga {

class Player; // Forward decl: applyTo() takes a Player& (resolved in the .cpp).

/// A weapon pickup that sets the Player's attack and ranged capability (R19.2).
class Weapon : public Item {
public:
    /// Construct a weapon at a grid position with its combat properties.
    /// @param position   the grid cell the weapon lies on until picked up.
    /// @param attackValue the attack value the hero gains while wielding this
    ///        weapon (it replaces the hero's current attack on equip).
    /// @param ranged     true for a ranged weapon (fires projectiles that
    ///        consume ammo, R16), false for a melee weapon.
    /// @param rangeBonus extra cells of fire range this weapon grants the hero
    ///        on equip (Feature 1). Defaults to 0 so a melee weapon (or any
    ///        weapon built without a bonus) leaves the hero's reach unchanged;
    ///        the WaveManager passes Config::fireRangeUpgradeBonus() for ranged
    ///        weapons so picking one up extends fire range.
    Weapon(const Vec2& position, int attackValue, bool ranged,
           int rangeBonus = 0);

    /// Equip this weapon on the player (R19.2): point the player at this weapon
    /// and set the player's attack value to this weapon's attack value. Because
    /// the player keeps a non-owning pointer to the equipped weapon, the combat
    /// system can later consult isRanged() to decide between melee and a
    /// projectile/ammo path. When this weapon carries a positive rangeBonus
    /// (Feature 1) the player's fire range is also extended via addFireRange().
    /// @param player the hero equipping the weapon (modified in place).
    void applyTo(Player& player) override;

    /// @return false: a weapon stays equipped after pickup (not consumed).
    bool isConsumable() const override;

    /// @return '/', the ASCII icon for a weapon.
    char glyph() const override;

    /// @return the attack value this weapon grants the wielder.
    int attackValue() const;

    /// @return true when this is a ranged weapon (consumes ammo to fire, R16).
    bool isRanged() const;

    /// @return how many extra cells of fire range this weapon grants on equip
    ///         (Feature 1); 0 for weapons that do not extend reach.
    int rangeBonus() const;

private:
    int attackValue_; ///< Attack value granted to the wielder on equip (R19.2).
    bool ranged_;     ///< Whether firing this weapon uses ammo/projectiles (R16).
    int rangeBonus_;  ///< Extra fire-range cells granted on equip (Feature 1).
};

} // namespace dga
