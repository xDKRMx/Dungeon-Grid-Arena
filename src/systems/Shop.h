// =============================================================================
// systems/Shop.h
//
// Purpose:
//   Shop is the stateless service that runs the post-wave PAID purchase phase
//   (Feature 2). It is a deliberate sibling of UpgradeSystem rather than a
//   merger:
//
//     * UpgradeSystem (the FREE draft) hands the player ONE of three random
//       cards drawn from the upgrade pool. Cards are unconditional: pick one
//       and the effect is applied (R23).
//     * Shop (the PAID draft) presents three rotating items costed in Gold
//       (Feature 1). The player either buys one (gold deducted, effect
//       applied) or skips. If gold is insufficient the row is unaffordable
//       but still drawn so the player can see what they missed.
//
//   Keeping the two systems separated keeps the Shop's currency / pricing
//   logic out of UpgradeSystem and protects each from the other's evolution
//   (the Shop's catalogue is going to grow; the free draft is balanced for a
//   small predictable pool).
//
//   Item pool (each at a distinct cost; see ShopItem::Effect for the runtime
//   wiring):
//     * "Piercer Round" — 30g — WallPierceShot, value 3
//         Next 3 fire shots pass THROUGH walls instead of stopping at them.
//     * "Quickstep"     — 25g — DoubleMoveTurn,  value 4
//         Next 4 turns the player gets 2 movement actions per turn.
//     * "Blink Chain"   — 40g — BlinkChain,      value 3
//         Next 3 Blink uses bypass the ability cooldown entirely.
//     * "Twin Strike"   — 35g — TwinStrike,      value 5
//         Next 5 melee hits deal 2× damage.
//     * "Coin Cache"    —  0g — BonusGold,       value 0
//         Always-affordable filler so the menu always has 3 valid items even
//         when the costs of others are out of reach. No effect on purchase
//         (there is nothing to give: the player already has the gold).
//
//   The draft picks 3 distinct items via a partial Fisher-Yates shuffle of the
//   pool, mirroring UpgradeSystem::draftCards so the two phases share their
//   randomness pattern.
//
// Why a .h/.cpp split:
//   Shop carries real logic (random draft, effect dispatch); declarations live
//   here and definitions in Shop.cpp (R2.1).
//
// Layer: systems (depends on entities/Player, systems/GameState, core/Rng).
// =============================================================================
#pragma once

#include <string>  // std::string - item name and description.
#include <vector>  // std::vector - the pool / draft return type.

namespace dga {

class GameState;
class Player;
class Rng;

// =============================================================================
// ShopItem - one purchasable entry presented in the Shop draft.
// =============================================================================

/// One purchasable Shop item (Feature 2).
///
/// Each item carries a human-readable label, a detailed description, the gold
/// cost the buyer must pay, an Effect tag the purchase() dispatch branches on,
/// and an integer value that scales the effect (number of charges, turns,
/// damage tier, ...).
struct ShopItem {
    /// The category of post-purchase effect this item grants. The dispatch in
    /// Shop::purchase reads this tag and writes the corresponding charge
    /// counter on Player (or, for BonusGold, mints gold directly).
    enum class Effect {
        WallPierceShot, ///< Add `value` Piercer Round charges to the player.
        DoubleMoveTurn, ///< Add `value` Quickstep charges to the player.
        BlinkChain,     ///< Add `value` Blink Chain uses to the player.
        TwinStrike,     ///< Add `value` Twin Strike melee charges to the player.
        BonusGold       ///< Add `value` gold to GameState's wallet (filler item).
    };

    std::string name;        ///< Short display name, e.g. "Piercer Round".
    std::string description; ///< One-line shop description (effect summary).
    int         cost;        ///< Gold price; 0 means always affordable.
    Effect      effect;      ///< Which buff the purchase grants.
    int         value;       ///< Magnitude (charges / turns / gold to mint).
};

// =============================================================================
// Shop - stateless service that runs the post-wave purchase menu.
// =============================================================================

/// Stateless service that runs the paid Shop phase after each wave (Feature 2).
///
/// All member functions are const (no object state). The run's Rng is supplied
/// by the caller (owned by GameState) so draws are deterministic for a given
/// seed (R26.4).
class Shop {
public:
    /// Default constructor; the class holds no data members.
    Shop() = default;

    // ---- Draft (Feature 2) -------------------------------------------------

    /// Draw 3 DISTINCT shop items at random from the available catalogue.
    ///
    /// Distinctness is guaranteed by sampling without replacement: a partial
    /// Fisher-Yates shuffle moves the chosen index out of the candidate
    /// portion before the next draw, mirroring UpgradeSystem::draftCards.
    ///
    /// The pool always contains the full catalogue (including the
    /// always-affordable "Coin Cache" filler, see the file header) so the
    /// returned hand never has fewer than 3 items unless the catalogue itself
    /// shrinks below 3.
    ///
    /// @param rng the run's deterministic randomness source.
    /// @return a vector of exactly 3 (or fewer if the catalogue is smaller)
    ///         distinct ShopItems suitable for display in a menu.
    std::vector<ShopItem> draftItems(Rng& rng) const;

    // ---- Purchase (Feature 2) ---------------------------------------------

    /// Apply the chosen item's effect to the player and the run state.
    ///
    /// The caller is responsible for verifying affordability and DEDUCTING
    /// the cost from the player's gold reserve BEFORE calling purchase — this
    /// keeps the purchase function focused on a single concern (effect
    /// dispatch) and matches the way UpgradeSystem::apply behaves.
    ///
    /// Effect dispatch:
    ///   * WallPierceShot → player.addWallPierceShots(value)
    ///   * DoubleMoveTurn → player.addDoubleMoveTurns(value)
    ///   * BlinkChain     → player.addBlinkChainUses(value)
    ///   * TwinStrike     → player.addTwinStrikeCharges(value)
    ///   * BonusGold      → state.addGold(value) (filler always-affordable item)
    ///
    /// @param item   the item the player chose.
    /// @param player the hero to apply the buff to.
    /// @param state  the authoritative game state (used by BonusGold).
    void purchase(const ShopItem& item, Player& player, GameState& state) const;
};

} // namespace dga
