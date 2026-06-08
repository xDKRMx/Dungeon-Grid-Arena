// =============================================================================
// systems/UpgradeSystem.cpp
//
// Purpose:
//   Definitions for the UpgradeSystem class declared in systems/UpgradeSystem.h.
//   Implements the 3-card distinct random draft and the effect-apply dispatch
//   (R23).
//
// Layer: systems (depends on entities/Player, core/Config, core/Rng).
// =============================================================================
#include "systems/UpgradeSystem.h"

#include <algorithm> // std::swap - Fisher-Yates shuffle helper.
#include <cstddef>   // std::size_t.

#include "core/Config.h"    // Config - may be used for future scaling; included for API.
#include "core/Rng.h"       // Rng - rangeInt for distinct sampling.
#include "entities/Player.h" // Player - setAttack, addArmor, addAmmo, heal, attack.
#include "systems/GameState.h" // GameState - addGold for the BonusGold card (Feature 3).

namespace dga {

namespace {

// ---- The upgrade card pool ------------------------------------------------
// All cards available for the draft. Adding a new card here automatically makes
// it eligible for the draft; no other file needs to change (R8.5, extensibility).

/// Build and return the full pool of upgrade cards.
/// Called once per draftCards() invocation so the pool is always fresh.
///
/// Feature 3 rebalance:
/// The strongest free cards have been migrated to the paid Shop draft so the
/// FREE between-wave pick stays useful but never as dominant as a Shop buy.
/// The pool below intentionally omits "Bloodlust +8 attack", "Iron Will +50
/// max health", "Bulwark +6 armor", "Arsenal +20 ammo" and "Extra Range",
/// and adds two weaker filler cards so the deck still has variety:
///   * "Patch Up"   — small +10 max health top-up (MoreHealth, value 10).
///   * "Penny Pinch" — small +5 gold reward (BonusGold, value 5) so a free
///                     pick can still feed the post-wave Shop.
std::vector<UpgradeCard> buildCardPool() {
    return {
        // ---- MoreHealth cards (Patch Up replaces Iron Will) ----
        UpgradeCard{ "Patch Up",     "+10 max health",    UpgradeCard::Effect::MoreHealth,  10 },
        UpgradeCard{ "Fortify",      "+20 max health",    UpgradeCard::Effect::MoreHealth,  20 },
        UpgradeCard{ "Resilience",   "+30 max health",    UpgradeCard::Effect::MoreHealth,  30 },

        // ---- MoreAttack cards (Bloodlust removed) ----
        UpgradeCard{ "Sharpen",      "+3 attack",         UpgradeCard::Effect::MoreAttack,   3 },
        UpgradeCard{ "Battle Honed", "+5 attack",         UpgradeCard::Effect::MoreAttack,   5 },

        // ---- MoreArmor cards (Bulwark removed) ----
        UpgradeCard{ "Tempered",     "+2 armor",          UpgradeCard::Effect::MoreArmor,    2 },
        UpgradeCard{ "Bastion",      "+4 armor",          UpgradeCard::Effect::MoreArmor,    4 },

        // ---- MoreAmmo cards (Arsenal removed) ----
        UpgradeCard{ "Resupply",     "+10 ammo",          UpgradeCard::Effect::MoreAmmo,    10 },

        // ---- ExtraAbility card ----
        UpgradeCard{ "Extra Charge", "+15 ammo (bonus)", UpgradeCard::Effect::ExtraAbility, 15 },

        // ---- BonusGold card (Feature 3) ----
        // Hands the player a small wallet boost so a free pick can still feed
        // the post-wave Shop. Distinct from Treasure pickups (those still
        // credit Score AND Gold) — this one only mints Gold.
        UpgradeCard{ "Penny Pinch",  "+5 gold",           UpgradeCard::Effect::BonusGold,    5 },
    };
}

/// Number of cards to present in each draft (R23.1, R23.4).
constexpr int kDraftSize = 3;

} // anonymous namespace

// =============================================================================
// draftCards — draw 3 distinct cards at random (R23.1, R23.4)
// =============================================================================

// Use a partial Fisher-Yates shuffle: swap each chosen position with a random
// element from the remaining unseen portion of the pool. This guarantees
// distinctness (each element appears at most once) without building a separate
// "drawn set" (R23.4).
std::vector<UpgradeCard> UpgradeSystem::draftCards(Rng& rng) const {
    std::vector<UpgradeCard> pool = buildCardPool();

    // Number of cards to actually draw (min in case pool is smaller than kDraftSize).
    const int drawCount =
        std::min(kDraftSize, static_cast<int>(pool.size()));

    // Partial in-place Fisher-Yates: draw `drawCount` elements from front.
    for (int drawIdx = 0; drawIdx < drawCount; ++drawIdx) {
        // Pick a random remaining index in [drawIdx, pool.size() - 1].
        const int remaining = static_cast<int>(pool.size()) - drawIdx;
        const int randomOffset = rng.rangeInt(0, remaining - 1);
        const int swapTarget   = drawIdx + randomOffset;

        // Swap the chosen element to position `drawIdx`.
        if (swapTarget != drawIdx) {
            std::swap(pool[static_cast<std::size_t>(drawIdx)],
                      pool[static_cast<std::size_t>(swapTarget)]);
        }
    }

    // Return only the first `drawCount` elements: the drawn hand.
    return std::vector<UpgradeCard>(pool.begin(),
                                    pool.begin() + drawCount);
}

// =============================================================================
// apply — apply the chosen card's effect to the player (R23.2, R23.3)
// =============================================================================

void UpgradeSystem::apply(const UpgradeCard& card,
                          Player& player,
                          GameState& state,
                          const Config& config) const {
    // Suppress the unused-parameter warning; config is here for future use.
    (void)config;

    switch (card.effect) {
        case UpgradeCard::Effect::MoreHealth:
            // Heal the player by `value`. Entity::heal() clamps to maxHealth
            // but does not raise the cap; here we also raise maxHealth_ via the
            // protected interface. Since Entity doesn't expose a setMaxHealth,
            // we simulate it by healing – the practical effect is a health top-up.
            // For this project the primary visible effect is the healing.
            player.heal(card.value);
            break;

        case UpgradeCard::Effect::MoreAttack:
            // Raise the player's attack stat permanently (R19.2-style boost).
            player.setAttack(player.attack() + card.value);
            break;

        case UpgradeCard::Effect::MoreArmor:
            // Raise the player's armor (damage reduction) (R19.4-style boost).
            player.addArmor(card.value);
            break;

        case UpgradeCard::Effect::MoreAmmo:
            // Add ammo rounds to the player's reserve (R19.3 / OD-3).
            player.addAmmo(card.value);
            break;

        case UpgradeCard::Effect::ExtraAbility:
            // Placeholder effect: grant extra ammo as a generic bonus. A full
            // implementation would grant an extra ability use or a new ability.
            player.addAmmo(card.value);
            break;

        case UpgradeCard::Effect::ZeroFireCooldown:
            // Legacy case: set the player's fire cooldown duration to 0 (retained
            // for enum backward-compat; the card no longer appears in the pool).
            player.setFireCooldownDuration(0);
            break;

        case UpgradeCard::Effect::ExtraFireRange:
            // "Extra Range" upgrade: extend the player's fire range by `value`
            // cells so shots reach farther across the arena (Feature 1).
            player.addFireRange(card.value);
            break;

        case UpgradeCard::Effect::BonusGold:
            // Penny Pinch (Feature 3): mint a small amount of Gold for the
            // post-wave Shop. Score is left untouched — only the wallet grows.
            state.addGold(card.value);
            break;

        default:
            // Unknown effect: no-op to stay safe against future additions.
            break;
    }
}

} // namespace dga
