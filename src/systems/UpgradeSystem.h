// =============================================================================
// systems/UpgradeSystem.h
//
// Purpose:
//   UpgradeSystem presents 3 distinct upgrade cards at the end of each wave and
//   applies the chosen card's effect to the player (R23). It embodies the
//   between-wave "build" phase of the roguelike loop.
//
//   Responsibilities:
//     * draftCards() — draw 3 distinct UpgradeCards from the pool at random
//                      (R23.1, R23.4). Uses the run's deterministic Rng so the
//                      same seed always produces the same draft.
//     * apply()      — apply the chosen card's effect to the Player and discard
//                      the unselected cards (R23.2, R23.3). The effect is a
//                      simple stat boost selected by the card's Effect enum.
//
//   UpgradeCard is a plain struct defined here (not in a separate header) because
//   it is used exclusively by UpgradeSystem and its callers.
//
// Requirements: 23.1, 23.2, 23.3, 23.4
//
// Why a .h/.cpp split:
//   UpgradeSystem contains real logic (random card selection, effect dispatch);
//   declarations live here and definitions in UpgradeSystem.cpp (R2.1).
//
// Layer: systems (depends on entities/Player, core/Config, core/Rng).
// =============================================================================
#pragma once

#include <string>  // std::string - card name and description.
#include <vector>  // std::vector - the pool of available cards and the draft result.

namespace dga {

class Config;
class GameState;
class Player;
class Rng;

// =============================================================================
// UpgradeCard — one selectable upgrade option presented to the player.
// =============================================================================

/// A single upgrade option shown during the between-wave draft (R23.1).
///
/// Each card has a human-readable name and description, a machine-readable
/// Effect tag that the UpgradeSystem dispatches on, and an integer value that
/// scales the effect (e.g., +20 HP, +5 attack, +2 armor, ...).
struct UpgradeCard {
    /// The category of upgrade effect the card grants.
    enum class Effect {
        MoreHealth,       ///< Heal and increase max Health by `value` HP (R19.1-style).
        MoreAttack,       ///< Increase base attack by `value` (R19.2-style).
        MoreArmor,        ///< Increase armor (damage reduction) by `value` (R19.4-style).
        MoreAmmo,         ///< Add `value` ammo rounds to the reserve (R19.3-style / OD-3).
        ExtraAbility,     ///< Grant the player one more use of an ability (future hook;
                          ///< implemented as a generic bonus for now).
        ZeroFireCooldown, ///< Legacy: set fire cooldown duration to 0 (card removed from pool).
        ExtraFireRange,   ///< Extend fire range by `value` cells (replaces Rapid Fire card).
        BonusGold         ///< Add `value` gold to the player's wallet (Feature 3 filler card).
    };

    std::string name;        ///< Short display name, e.g. "Fortify".
    std::string description; ///< One-line description, e.g. "+20 max health".
    Effect effect;           ///< Which stat the card modifies.
    int value;               ///< Magnitude of the effect.
};

// =============================================================================
// UpgradeSystem
// =============================================================================

/// Stateless service that manages the between-wave upgrade draft (R23).
///
/// All member functions are const (no object state). The run's Rng is supplied
/// by the caller (owned by GameState) so draws are deterministic (R26.4).
class UpgradeSystem {
public:
    /// Default constructor; the class holds no data members.
    UpgradeSystem() = default;

    // ---- Draft (R23.1, R23.4) ----------------------------------------------

    /// Draw 3 DISTINCT upgrade cards at random from the available pool (R23.1,
    /// R23.4). Distinctness is guaranteed by sampling without replacement: once
    /// a card is drawn its index is removed from the candidate set before the
    /// next draw.
    ///
    /// The pool is always the full set of defined UpgradeCards; cards are never
    /// permanently removed (each wave draws fresh from the whole pool). If the
    /// pool has fewer than 3 cards, fewer are returned (no duplicates are
    /// introduced to fill the gap).
    ///
    /// @param rng the run's deterministic randomness source; the same seed
    ///            produces the same draft (R26.4).
    /// @return a vector of exactly 3 (or fewer if the pool is small) distinct
    ///         UpgradeCards (R23.4).
    std::vector<UpgradeCard> draftCards(Rng& rng) const;

    // ---- Apply (R23.2, R23.3) ----------------------------------------------

    /// Apply the chosen card's effect to the Player and discard the rest (R23.2).
    ///
    /// Effect dispatch:
    ///   * MoreHealth  → heal the player by `card.value`, raising current and
    ///                   max Health (clamp to maxHealth is Entity::heal's job).
    ///   * MoreAttack  → call player.setAttack(player.attack() + card.value).
    ///   * MoreArmor   → call player.addArmor(card.value).
    ///   * MoreAmmo    → call player.addAmmo(card.value).
    ///   * ExtraAbility→ add ammo as a generic bonus (placeholder; a real
    ///                   implementation would grant an extra ability use).
    ///   * BonusGold   → call state.addGold(card.value) — the Penny Pinch card
    ///                   (Feature 3) hands the player a small Gold reward for
    ///                   the post-wave Shop. This is the reason `state` is
    ///                   threaded into apply: the gold lives on GameState
    ///                   (Feature 1), not on the Player.
    ///
    /// "Discard the unselected cards" is implicit: the caller owns the drafted
    /// vector and simply drops the two un-chosen cards when the vector goes out
    /// of scope. This function only applies the ONE card passed to it (R23.2).
    ///
    /// @param card   the upgrade card the player selected.
    /// @param player the hero to apply the upgrade to.
    /// @param state  the run state, modified by the BonusGold dispatch (Feature 3).
    /// @param config balancing configuration (may be used for future effect
    ///               scaling; currently unused but included for extensibility).
    void apply(const UpgradeCard& card,
               Player& player,
               GameState& state,
               const Config& config) const;
};

} // namespace dga
