// =============================================================================
// items/HealthPotion.cpp
//
// Purpose:
//   Definitions for HealthPotion (declared in items/HealthPotion.h). This .cpp
//   includes the full "entities/Player.h" because applyTo() calls Player's
//   members - the forward declaration in the header is not enough here.
//
// Layer: items (depends on items/Item, entities/Player).
// =============================================================================
#include "items/HealthPotion.h"

#include "entities/Player.h" // full Player type so we can call heal()/maxHealth().

namespace dga {

// Tag the base as a HealthPotion and place it on the grid.
HealthPotion::HealthPotion(const Vec2& position)
    : Item(ItemKind::HealthPotion, position) {}

// Heal "up to the maximum" (R19.1). We heal by the player's full maxHealth;
// Entity::heal() clamps the result to maxHealth, so a partially hurt hero ends
// at full and a full hero is unchanged. This keeps the "up to max" rule in one
// place (the clamp) rather than re-deriving the missing amount here.
//
// Bonus: every potion also grants a brief 2-turn shield buff on top of the heal,
// making potions more tactically valuable by giving temporary damage immunity
// immediately after the heal. The shield stacks by overwriting (refresh), which
// mirrors how Player::applyShield works (R21.6).
void HealthPotion::applyTo(Player& player) {
    player.heal(player.maxHealth());

    // Bonus shield: 2 turns of damage immunity on every potion pickup.
    constexpr int kPotionShieldTurns = 2;
    player.applyShield(kPotionShieldTurns);
}

// A potion is one-shot: true tells the Inventory to drop it after use (R20.3).
bool HealthPotion::isConsumable() const {
    return true;
}

char HealthPotion::glyph() const {
    return '!';
}

} // namespace dga
