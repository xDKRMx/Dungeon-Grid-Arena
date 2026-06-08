// =============================================================================
// systems/Shop.cpp
//
// Purpose:
//   Definitions for the Shop service declared in systems/Shop.h. Implements
//   the 3-item distinct random draft and the effect-apply dispatch used by the
//   post-wave Shop menu (Feature 2).
//
// Layer: systems (depends on entities/Player, systems/GameState, core/Rng).
// =============================================================================
#include "systems/Shop.h"

#include <algorithm> // std::swap - Fisher-Yates shuffle helper.
#include <cstddef>   // std::size_t.

#include "core/Rng.h"          // Rng - rangeInt for distinct sampling.
#include "entities/Player.h"   // Player - addX / consumeX accessors for buffs.
#include "systems/GameState.h" // GameState - addGold for the BonusGold effect.

namespace dga {

namespace {

// ---- Catalogue --------------------------------------------------------------
//
// All items the Shop may surface. Costs are deliberately distinct and rise
// with the strength of the effect, so the player has to read costs as a
// difficulty signal. "Coin Cache" is the 0-gold always-affordable filler that
// lets the menu always show three buyable rows even when an unlucky draft
// pairs the two priciest items together; it has no effect on purchase since
// the player ALREADY has the gold the row claims to give.

/// Build the full Shop catalogue. Called fresh per draftItems() invocation so
/// the pool can never be mutated across calls.
std::vector<ShopItem> buildShopPool() {
    return {
        ShopItem{ "Piercer Round",
                  "next 3 shots pierce walls",
                  30, ShopItem::Effect::WallPierceShot, 3 },

        ShopItem{ "Quickstep",
                  "next 4 turns: 2 moves per turn",
                  25, ShopItem::Effect::DoubleMoveTurn, 4 },

        ShopItem{ "Blink Chain",
                  "next 3 Blinks ignore cooldown",
                  40, ShopItem::Effect::BlinkChain, 3 },

        ShopItem{ "Twin Strike",
                  "next 5 melee hits deal 2x damage",
                  35, ShopItem::Effect::TwinStrike, 5 },

        ShopItem{ "Coin Cache",
                  "small change — costs nothing",
                  0,  ShopItem::Effect::BonusGold, 0 },
    };
}

/// Number of items the Shop draft presents to the player each wave. Three
/// matches the free Upgrade draft so the two between-wave menus feel
/// consistent and the player learns one shape of choice.
constexpr int kShopDraftSize = 3;

} // anonymous namespace

// =============================================================================
// draftItems - draw 3 distinct items at random
// =============================================================================

// Use a partial Fisher-Yates shuffle: swap each chosen position with a random
// element from the remaining unseen portion of the pool. This guarantees
// distinctness (each element appears at most once) without an extra "drawn
// set" container, mirroring UpgradeSystem::draftCards exactly.
std::vector<ShopItem> Shop::draftItems(Rng& rng) const {
    std::vector<ShopItem> pool = buildShopPool();

    // Number to actually draw (min in case the pool is somehow smaller).
    const int drawCount =
        (static_cast<int>(pool.size()) < kShopDraftSize)
            ? static_cast<int>(pool.size())
            : kShopDraftSize;

    // Partial in-place Fisher-Yates: draw `drawCount` elements from the front.
    for (int drawIdx = 0; drawIdx < drawCount; ++drawIdx) {
        const int remaining    = static_cast<int>(pool.size()) - drawIdx;
        const int randomOffset = rng.rangeInt(0, remaining - 1);
        const int swapTarget   = drawIdx + randomOffset;

        if (swapTarget != drawIdx) {
            std::swap(pool[static_cast<std::size_t>(drawIdx)],
                      pool[static_cast<std::size_t>(swapTarget)]);
        }
    }

    return std::vector<ShopItem>(pool.begin(),
                                 pool.begin() + drawCount);
}

// =============================================================================
// purchase - apply the chosen item's effect to the player / run state
// =============================================================================
//
// The caller (Game::runShop) is responsible for the affordability check and for
// deducting the gold cost BEFORE this function runs. Keeping the purchase
// function single-purpose mirrors UpgradeSystem::apply and makes effect
// dispatch easy to test in isolation.

void Shop::purchase(const ShopItem& item,
                    Player& player,
                    GameState& state) const {
    switch (item.effect) {
        case ShopItem::Effect::WallPierceShot:
            // Grant Piercer Round charges. CombatSystem reads the charge in
            // firePlayerProjectile and skips the wall-stop branch while > 0.
            player.addWallPierceShots(item.value);
            break;

        case ShopItem::Effect::DoubleMoveTurn:
            // Grant Quickstep charges. TurnManager reads the charge after a
            // successful Move and refrains from advancing the turn while > 0.
            player.addDoubleMoveTurns(item.value);
            break;

        case ShopItem::Effect::BlinkChain:
            // Grant Blink Chain uses. AbilitySystem zeroes the Blink cooldown
            // after a successful activation while > 0, decrementing per use.
            player.addBlinkChainUses(item.value);
            break;

        case ShopItem::Effect::TwinStrike:
            // Grant Twin Strike charges. CombatSystem doubles the dealt
            // melee damage while > 0, decrementing per consumed strike.
            player.addTwinStrikeCharges(item.value);
            break;

        case ShopItem::Effect::BonusGold:
            // The "Coin Cache" filler hands the player a token amount of
            // gold (currently 0 by design — its only purpose is to keep the
            // menu populated when other costs are out of reach). value() is
            // still added so future reskins of the filler can hand out a few
            // coins without touching this dispatch.
            state.addGold(item.value);
            break;
    }
}

} // namespace dga
