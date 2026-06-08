// =============================================================================
// abilities/Ability.h
//
// Purpose:
//   Ability is the abstract base class for every player-activated action that
//   has a cooldown: Dash, Nova, Shield, and Blink (R4.4, R21). It owns the
//   bookkeeping that EVERY ability shares - which kind it is, how long its
//   cooldown lasts, and how many Turns remain before it is ready again - so the
//   AbilitySystem can tick, query, and activate any ability uniformly through an
//   Ability base reference or pointer.
//
//   This is one of the project's inheritance/polymorphism showcases (R4.4):
//   each concrete ability derives from Ability, the destructor is virtual so an
//   ability can be safely deleted through a base pointer (R4.7), and activate()
//   is a pure virtual hook so each ability performs its own effect via virtual
//   dispatch.
//
// Scope of THIS task (4.6): the cooldown machinery is implemented fully and
//   correctly here (isReady/tick/putOnCooldown, R21.2/R21.4). The concrete
//   *effects* of each ability (moving the hero, damaging enemies, granting
//   immunity, teleporting) need the full GameState/map/enemy context, which is
//   wired in task 8.1. Until then activate() carries a clearly-marked
//   placeholder body in each subclass; the signature is intentionally kept
//   parameterless for now and will be widened to take the game context in 8.1.
//
// Why a .h/.cpp split:
//   Ability owns real logic (the cooldown state machine), so its declarations
//   live here and its definitions live in Ability.cpp (R2.1, R4.7).
//
// Layer: abilities (this base depends only on core/Enums.h for AbilityKind;
//   the concrete subtypes additionally read durations from core/Config).
// =============================================================================
#pragma once

#include "core/Enums.h" // AbilityKind - tags which concrete ability this is.

namespace dga {

/// Abstract base for any cooldown-gated player ability (R4.4, R21).
///
/// Shared state is `protected` so subclasses can read it while outside code
/// goes through the public accessors (R1.2). The class is abstract because
/// activate() is pure virtual: you can never construct a bare Ability, only one
/// of the concrete subtypes.
class Ability {
public:
    /// Construct an ability with its kind tag and cooldown length.
    /// @param kind             which concrete ability this is (Dash, Nova, ...);
    ///                         stored so the AbilitySystem and HUD can branch on
    ///                         the category without a dynamic_cast.
    /// @param cooldownDuration how many Turns the ability stays on cooldown
    ///                         after it is used (R21.4). A freshly built ability
    ///                         starts READY: its remaining cooldown is 0.
    ///                         Nova passes 0 here because it is gated by the
    ///                         Charge_Meter, not by a turn cooldown (R22).
    Ability(AbilityKind kind, int cooldownDuration);

    /// Virtual destructor so deleting a concrete ability through an `Ability*`
    /// (the Player owns abilities as `unique_ptr<Ability>`) runs the correct
    /// destructor (R4.7). Defaulted in Ability.cpp.
    virtual ~Ability();

    /// @return which concrete kind of ability this is (R4 polymorphism tag).
    AbilityKind kind() const;

    /// Report whether the ability can be used this Turn.
    /// @return true exactly when no cooldown Turns remain (R21.2). An ability
    ///         gated by the Charge_Meter (Nova) has a 0-length cooldown, so by
    ///         this measure it is always "ready"; the AbilitySystem layers the
    ///         separate charge check on top (R22.3).
    bool isReady() const;

    /// @return the number of Turns still remaining before the ability is ready.
    int cooldownRemaining() const;

    /// @return the full cooldown length this ability is put on when used.
    int cooldownDuration() const;

    /// Advance the cooldown by one Turn (R21.4). Decrements the remaining count
    /// toward 0 and never below it, so calling tick() on a ready ability is a
    /// harmless no-op. The AbilitySystem calls this once per Turn for every
    /// ability the hero owns.
    void tick();

    /// Put the ability on a full cooldown (R21.4). Sets the remaining count to
    /// the configured duration; called right after a successful activation so
    /// the ability cannot be reused until it has ticked back down to ready.
    void putOnCooldown();

    /// Reset the remaining cooldown to 0 (READY) without altering the
    /// configured duration. Used by the Blink Chain shop buff (Feature 2):
    /// after a Blink fires the AbilitySystem normally calls putOnCooldown(),
    /// but while the player has Blink Chain charges remaining the cooldown
    /// is immediately cleared with this call so the ability is ready again
    /// next turn. Idempotent / safe on an already-ready ability.
    void clearCooldown();

    /// Perform the ability's effect (R21). Pure virtual hook overridden by each
    /// concrete ability, so a single Ability base call dispatches to the right
    /// effect without a type switch.
    ///
    /// NOTE: the effect bodies are placeholders in this task and are fleshed out
    /// in task 8.1, where the ability gains access to the GameState (map,
    /// player, enemies) it needs to act on. The cooldown wiring around it is
    /// already complete and correct.
    virtual void activate() = 0;

protected:
    AbilityKind kind_;       ///< Category tag for this concrete ability (R4).
    int cooldownRemaining_;  ///< Turns left until ready; 0 means usable (R21.2).
    int cooldownDuration_;   ///< Turns this ability is locked for after use (R21.4).
};

} // namespace dga
