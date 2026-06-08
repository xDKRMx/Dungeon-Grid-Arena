// =============================================================================
// items/Weapon.cpp
//
// Purpose:
//   Definitions for Weapon (declared in items/Weapon.h). Includes the full
//   "entities/Player.h" because applyTo() calls Player::equip()/setAttack().
//
// Layer: items (depends on items/Item, entities/Player).
// =============================================================================
#include "items/Weapon.h"

#include "entities/Player.h" // full Player type so we can equip / set attack.

namespace dga {

// Tag the base as a Weapon, place it on the grid, and remember its combat props.
Weapon::Weapon(const Vec2& position, int attackValue, bool ranged,
               int rangeBonus)
    : Item(ItemKind::Weapon, position),
      attackValue_(attackValue),
      ranged_(ranged),
      rangeBonus_(rangeBonus) {}

// Equipping does up to three things (R19.2, Feature 1): it registers this weapon
// as the player's equipped weapon (so the combat system can later read
// isRanged()), conditionally raises the player's attack to this weapon's value,
// and — when the weapon carries a positive range bonus — extends the player's
// fire range. equip() takes a reference, so we pass *this. addFireRange ignores
// a 0 bonus, so a melee weapon leaves reach untouched.
//
// Bug-fix history (Bug 2, "Weapon pickup REDUCES damage"):
//
//   The original applyTo unconditionally OVERWROTE the player's attack with
//   this weapon's value:
//
//       player.setAttack(attackValue_);
//
//   That was always wrong as a pickup effect — a freshly built melee weapon
//   carries an attackValue in [kWeaponMinAttack, kWeaponMaxAttack] (originally
//   2-5) which is well below the hero's base attack (Config::playerStartingAttack
//   == 14). Picking up such a weapon therefore SILENTLY REDUCED the hero's
//   damage from 14 to a single-digit number, so the very next "Player hits
//   Melee" event log line read "for 5 dmg" instead of the expected 14.
//
//   The fix is a max-guard: only adopt the weapon's attack when it is a real
//   upgrade, i.e. strictly greater than the player's current attack. A pickup
//   can therefore only ever leave the hero stronger or equal, never weaker.
//   The weapon is still equipped (so isRanged() / hasSpreadShot() can be read
//   by the combat system), and the range bonus still applies — only the
//   attack-replacement step is gated.
//
//   This is paired with a balance pass in WaveManager.cpp that raises
//   kWeaponMinAttack / kWeaponMaxAttack so a melee weapon roll is now a real
//   upgrade above the hero's base attack rather than a downgrade — but the
//   max-guard here is defence-in-depth: even if a future tweak ever lowers
//   those bounds again, this function will never weaken the hero.
void Weapon::applyTo(Player& player) {
    player.equip(*this);

    // Only adopt this weapon's attack value if it is a strict upgrade over the
    // hero's current attack. This prevents a low-rolled melee weapon from
    // silently reducing the hero's damage on pickup (Bug 2).
    if (attackValue_ > player.attack()) {
        player.setAttack(attackValue_);
    }

    // Range bonus is still applied unconditionally (addFireRange itself ignores
    // a non-positive bonus, so melee weapons with rangeBonus_ == 0 are no-ops).
    player.addFireRange(rangeBonus_);
}

// A weapon persists after pickup; it is not consumed on use.
bool Weapon::isConsumable() const {
    return false;
}

char Weapon::glyph() const {
    return '/';
}

int Weapon::attackValue() const {
    return attackValue_;
}

bool Weapon::isRanged() const {
    return ranged_;
}

int Weapon::rangeBonus() const {
    return rangeBonus_;
}

} // namespace dga
