// =============================================================================
// entities/Player.h
//
// Purpose:
//   Player is the single hero the user controls (R4.1). It derives from Entity,
//   inheriting position/Health/attack/armor, and adds the things only the hero
//   has: an ammo count for ranged weapons (OD-3), a Charge_Meter that fills from
//   kills and powers the Nova ultimate (R22), an equipped Weapon, an Inventory
//   of picked-up items (R20), and the set of activatable Abilities it owns (R21).
//
// Ownership model (read once, it explains the pointer choices below):
//   - The Inventory is held BY VALUE: it is a small part of the player and lives
//     and dies with it.
//   - The equipped Weapon is a NON-OWNING pointer: the Weapon object is owned by
//     the GameState's item list; the player just points at whichever weapon is
//     currently equipped (may be null when unarmed-ranged). No delete here.
//   - The Abilities are OWNED via std::unique_ptr<Ability>: the player is the
//     natural owner of its own ability instances, and unique_ptr gives automatic
//     cleanup plus polymorphism through the Ability base (R21, R4.4).
//
// Dependency knot (resolved with forward declarations):
//   Weapon (items) and Ability (abilities) are only referenced by pointer here,
//   and Config (core) only by reference in the constructor, so all three are
//   forward-declared; the .cpp includes their full headers. This keeps the
//   Item <-> Player <-> Inventory include cycle broken.
//
// Why a .h/.cpp split:
//   Player carries real logic (ammo spending, equip), so declarations live here
//   and definitions live in Player.cpp (R2.1).
//
// Layer: entities (depends on core, entities/Entity, items/Inventory).
// =============================================================================
#pragma once

#include <memory> // std::unique_ptr - owns the player's Ability instances.
#include <vector> // std::vector     - the list of owned abilities.

#include "entities/Entity.h"  // Entity - the polymorphic base class.
#include "items/Inventory.h"  // Inventory - held by value as part of the player.

namespace dga {

class Config;  // Forward decl: the constructor reads starting stats from Config.
class Weapon;  // Forward decl: equippedWeapon_ is a non-owning Weapon* (see header).
class Ability; // Forward decl: abilities_ owns Ability subtypes polymorphically.

/// The hero entity controlled by the user (R4.1).
///
/// Player `final`-ly specializes Entity for the human side: it carries ammo,
/// a Charge_Meter, an equipped Weapon, an Inventory, and owned Abilities. All
/// data is private and reached through member functions (R1.2).
class Player : public Entity {
public:
    /// Construct the hero with its starting stats taken from Config (R15.1).
    /// Reading the numbers from Config keeps magic numbers out of the logic
    /// (R8.5): starting/max Health, base attack/armor, and starting ammo all
    /// come from the single configuration source. The Charge_Meter starts empty.
    /// @param config        the balancing configuration to read starting stats
    ///                       from (not stored; only read during construction).
    /// @param startPosition the grid cell the hero begins on.
    Player(const Config& config, const Vec2& startPosition);

    // ---- Ammo (ranged weapons, OD-3) --------------------------------------

    /// @return the hero's current ammunition count.
    int ammo() const;

    /// Add ammunition to the hero's reserve (e.g. from an Ammo_Item) (R19.3).
    /// @param amount the number of rounds to add; negative amounts are ignored
    ///        so a pickup can never silently reduce ammo.
    void addAmmo(int amount);

    /// Attempt to consume ammunition for a ranged attack (R16.2).
    /// @param amount the number of rounds to spend (typically 1).
    /// @return true and decrements the reserve when enough ammo is available;
    ///         false and leaves the reserve unchanged when the request is
    ///         negative or would drive ammo below 0 (R16.3 rejection). This is
    ///         what lets the combat system reject a shot with no ammo.
    bool spendAmmo(int amount);

    // ---- Fire range (Feature 1: limited + upgradeable ranged reach) -------

    /// @return how many cells a fired projectile may travel before it is
    ///         considered a miss. Initialised from Config::playerBaseFireRange()
    ///         in the constructor and raised by addFireRange() when a ranged
    ///         Weapon is equipped. The CombatSystem reads this as the stopping
    ///         distance for the player's ray instead of a fixed constant.
    int fireRange() const;

    /// Extend the hero's fire range by a positive bonus (Feature 1).
    ///
    /// Called by a ranged Weapon's applyTo() so picking up a ranged weapon both
    /// raises attack and lengthens reach. Non-positive bonuses are ignored so a
    /// pickup can only ever improve range, mirroring addAmmo()/addArmor().
    /// @param bonus the number of extra cells to add to the fire range.
    void addFireRange(int bonus);

    // ---- Charge meter (Nova ultimate resource, R22) -----------------------

    /// @return the current Charge_Meter value (Nova becomes usable at the max).
    int chargeMeter() const;

    /// Set the Charge_Meter to an exact value.
    /// @param newCharge the value to store. Clamping to the configured maximum
    ///        is the AbilitySystem's job (it knows the max from Config, R22.1);
    ///        the Player just stores whatever value it is given.
    void setChargeMeter(int newCharge);

    // ---- Equipped weapon (non-owning) -------------------------------------

    /// Equip a weapon, pointing the hero at it (R19.2).
    /// @param weapon the weapon to equip. Ownership is NOT transferred: the
    ///        GameState still owns the Weapon object; the player only references
    ///        it so attacks can read its stats / ranged capability.
    void equip(Weapon& weapon);

    /// @return a pointer to the currently equipped weapon, or nullptr when none
    ///         is equipped. The pointer is non-owning.
    Weapon* equippedWeapon() const;

    /// @return true when the player has a ranged weapon equipped, which grants
    ///         spread shot (fires in the chosen direction AND two adjacent
    ///         diagonals simultaneously). Used by CombatSystem to decide whether
    ///         to fire bonus side beams after the main projectile.
    bool hasSpreadShot() const;

    // ---- Combat-stat changes from item pickups ---------------------------

    /// Set the hero's attack value, used when a Weapon is equipped (R19.2).
    /// @param newAttack the attack value the equipped weapon grants. A Weapon's
    ///        applyTo() calls this so the hero's damage reflects the weapon it is
    ///        holding. (Entity::attack_ is protected, so this lives on Player -
    ///        a subclass - rather than being exposed on the base for everyone.)
    void setAttack(int newAttack);

    /// Increase the hero's armor (damage reduction), used by Armor pickups
    /// (R19.4).
    /// @param amount how much to add to the hero's armor; negative amounts are
    ///        ignored so a pickup can only ever improve defense.
    void addArmor(int amount);

    // ---- Inventory (R20) --------------------------------------------------

    /// @return a modifiable reference to the hero's inventory (so the pickup
    ///         logic can add/remove held items).
    Inventory& inventory();

    /// @return a read-only reference to the hero's inventory (for the HUD).
    const Inventory& inventory() const;

    // ---- Abilities (owned, R21) -------------------------------------------

    /// Give the hero ownership of a new ability instance.
    /// @param ability an owning pointer to the ability; the Player takes over
    ///        its lifetime. Stored polymorphically as the Ability base so the
    ///        ability system can tick/activate it through the base interface.
    void addAbility(std::unique_ptr<Ability> ability);

    /// @return a read-only view of the hero's owned abilities (for the HUD and
    ///         cooldown display).
    const std::vector<std::unique_ptr<Ability>>& abilities() const;

    /// @return a modifiable view of the hero's owned abilities (so the ability
    ///         system can tick cooldowns and activate them).
    std::vector<std::unique_ptr<Ability>>& abilities();

    // ---- Fire cooldown (UX polish: prevents spam-fire turn after turn) ----

    /// @return the number of Turns remaining before the hero can fire again.
    ///         Zero means the next Fire action is allowed; any positive value
    ///         means a cooldown is in effect and the CombatSystem will reject
    ///         the shot until tickFireCooldown() decrements it back to zero.
    int fireCooldown() const;

    /// Set the remaining fire cooldown to an exact number of Turns.
    ///
    /// Called by the CombatSystem after a successful shot to start the cooldown
    /// from the configured value (Config::playerFireCooldown). Negative inputs
    /// are clamped to zero so the field can never carry a nonsensical state.
    /// @param turns the number of Turns the hero must wait before firing again.
    void setFireCooldown(int turns);

    /// Decrement the fire cooldown by one Turn, clamped at zero.
    ///
    /// Called by the AbilitySystem at end-of-turn (alongside tickShield) so the
    /// fire cooldown counts down in lockstep with ability cooldowns. Calling on
    /// an already-zero counter is a safe no-op.
    void tickFireCooldown();

    // ---- Fire cooldown duration (how many turns the cooldown LASTS) --------

    /// @return the number of Turns the fire cooldown lasts when it is reset
    ///         after a successful shot. Initialised from Config::playerFireCooldown
    ///         and can be reduced to 0 by the "Rapid Fire" upgrade card,
    ///         effectively granting unlimited fire speed. CombatSystem reads
    ///         this (via player.fireCooldownDuration()) instead of pulling the
    ///         value from Config directly, so per-player upgrades take effect.
    int fireCooldownDuration() const;

    /// Override the fire cooldown duration (how long it LASTS after each shot).
    ///
    /// Called by the UpgradeSystem when the "Rapid Fire" upgrade card is chosen.
    /// Setting this to 0 means the hero can fire every turn with no cooldown.
    /// Negative inputs are clamped to zero.
    /// @param turns the new cooldown duration in Turns.
    void setFireCooldownDuration(int turns);

    // ---- Shield state (R21.6) --------------------------------------------

    /// Apply the shield, granting damage immunity for `turns` Turns (R21.6).
    /// Overwrites any existing remaining duration so re-activating the shield
    /// while it is still active refreshes it to the full new duration.
    /// @param turns the number of Turns the shield should remain active.
    void applyShield(int turns);

    /// Report whether the hero currently has an active damage-immunity shield.
    /// @return true while shieldRemainingTurns_ > 0 (R21.6).
    bool isShielded() const;

    /// Decrement the shield duration by one Turn, stopping at zero.
    /// Called by the AbilitySystem / TurnManager at end-of-turn so the shield
    /// counts down correctly (R21.6). Calling on an already-expired shield is
    /// a safe no-op.
    void tickShield();

    /// @return the number of Turns remaining on the active shield (0 = none).
    int shieldRemainingTurns() const;

    // ---- Shop-purchased temporary buffs (Feature 2) ----------------------
    //
    // Each Shop item grants a SHORT-DURATION combat buff: the player gets a
    // small charge counter that ticks down as the buff is consumed. The
    // counters live on the Player so every system that needs them
    // (CombatSystem for fire/melee, AbilitySystem for Blink, TurnManager for
    // movement) can read/decrement through the public accessors below
    // without crossing layers. All four counters default to 0 (no buff
    // active) and are added to by the Shop's purchase() effect dispatch.

    /// @return how many of the player's NEXT projectile shots will pierce
    ///         walls instead of stopping at them (Piercer Round, Feature 2).
    ///         CombatSystem reads this in firePlayerProjectile and skips the
    ///         wall-stop branch while the count is > 0, decrementing once
    ///         per shot fired with the buff active.
    int wallPierceShotsRemaining() const;

    /// Add charges to the wall-pierce buff (Piercer Round purchase).
    /// Negative deltas are ignored so the Shop can only ever grant charges.
    /// @param charges the number of pierce-wall shots to add.
    void addWallPierceShots(int charges);

    /// Decrement the wall-pierce charge counter by one, clamping at 0.
    /// CombatSystem calls this each time a fire actually consumes the buff
    /// (i.e. the projectile would have stopped at a wall but pierced through
    /// instead).
    void consumeWallPierceShot();

    /// @return how many of the player's NEXT turns grant TWO movement actions
    ///         instead of one (Quickstep, Feature 2). TurnManager reads this
    ///         on a successful Move and SKIPS the turn-consumed bookkeeping
    ///         while > 0, decrementing once per move that benefits from the
    ///         buff so the player effectively gets two free moves per
    ///         counter unit.
    int doubleMoveTurnsRemaining() const;

    /// Add charges to the double-move buff (Quickstep purchase).
    /// @param charges the number of doubled-move turns to add.
    void addDoubleMoveTurns(int charges);

    /// Decrement the double-move charge counter by one, clamping at 0.
    /// TurnManager calls this whenever a move benefits from the buff.
    void consumeDoubleMoveTurn();

    /// @return how many of the player's NEXT Blink uses fire WITHOUT going
    ///         on cooldown (Blink Chain, Feature 2). AbilitySystem reads this
    ///         after a successful Blink activation and forces the ability's
    ///         cooldown back to 0 when > 0, decrementing per use.
    int blinkChainUsesRemaining() const;

    /// Add charges to the blink-chain buff (Blink Chain purchase).
    /// @param charges the number of cooldown-skipping Blinks to add.
    void addBlinkChainUses(int charges);

    /// Decrement the blink-chain charge counter by one, clamping at 0.
    /// AbilitySystem calls this each time a Blink consumes the buff.
    void consumeBlinkChainUse();

    /// @return how many of the player's NEXT melee strikes deal 2× damage
    ///         (Twin Strike, Feature 2). CombatSystem reads this in the
    ///         applyAttack PLAYER-attacker branch and doubles the dealt
    ///         damage while > 0, decrementing per consumed strike.
    int twinStrikeChargesRemaining() const;

    /// Add charges to the twin-strike buff (Twin Strike purchase).
    /// @param charges the number of doubled-damage melee hits to add.
    void addTwinStrikeCharges(int charges);

    /// Decrement the twin-strike charge counter by one, clamping at 0.
    /// CombatSystem calls this each time a melee attack consumes the buff.
    void consumeTwinStrikeCharge();

    // ---- Polymorphic rendering --------------------------------------------

    /// @return the hero's ASCII symbol, '@', the classic roguelike player glyph.
    char glyph() const override;

private:
    int ammo_;                 ///< Rounds available for ranged weapons (OD-3).
    int fireRange_;            ///< Cells a fired projectile may travel (Feature 1).
    int chargeMeter_;          ///< Nova charge built from kills; gated at the max (R22).
    int shieldRemainingTurns_; ///< Turns of damage immunity remaining; 0 = no shield (R21.6).
    int fireCooldown_;         ///< Turns remaining before the next Fire is allowed; 0 = ready.
    int fireCooldownDuration_; ///< How many Turns the cooldown lasts on reset (upgradeable to 0).
    int wallPierceShotsRemaining_; ///< Piercer Round charges (Feature 2): shots that pass through walls.
    int doubleMoveTurnsRemaining_; ///< Quickstep charges (Feature 2): moves that don't end the turn.
    int blinkChainUsesRemaining_;  ///< Blink Chain charges (Feature 2): Blinks that bypass cooldown.
    int twinStrikeChargesRemaining_; ///< Twin Strike charges (Feature 2): melee hits dealing 2× damage.
    Weapon* equippedWeapon_;   ///< Non-owning pointer to the equipped weapon, or null.
    Inventory inventory_;      ///< Items the hero is carrying (held by value) (R20).
    std::vector<std::unique_ptr<Ability>> abilities_; ///< Owned abilities (R21).
};

} // namespace dga
