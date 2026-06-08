// =============================================================================
// entities/Entity.h
//
// Purpose:
//   Entity is the abstract base class for everything that lives on the dungeon
//   grid and can be hurt: the Player and every kind of Enemy (R4.1). It owns the
//   state that ALL actors share - where they stand, how much Health they have,
//   how hard they hit, and how much damage they soak - so that the combat system
//   can treat any fighter uniformly through an Entity reference or pointer.
//
//   This is the project's inheritance/polymorphism foundation (R4): Player and
//   Enemy derive from Entity, the destructor is virtual so an Entity can be
//   safely deleted through a base pointer (R4.7), and glyph() is pure virtual so
//   each concrete actor draws itself differently while the renderer only ever
//   talks to an Entity.
//
// Why a .h/.cpp split:
//   Entity carries real logic (clamping Health on damage/heal), so its
//   declarations live here and its definitions live in Entity.cpp, matching the
//   project's multi-file rule for non-trivial classes (R2.1).
//
// Layer: entities (depends on core/Vec2.h and core/Enums.h only).
// =============================================================================
#pragma once

#include "core/Enums.h" // EntityKind - tags which concrete actor this is.
#include "core/Vec2.h"  // Vec2 - the entity's grid position.

namespace dga {

/// Abstract base for any actor that occupies a tile and has Health (R4.1).
///
/// All shared state is `protected` so derived classes (Player, Enemy) can read
/// and adjust it directly, while outside code must go through the public member
/// functions (R1.2). The class is abstract because glyph() is pure virtual: you
/// can never construct a bare Entity, only a concrete subtype.
class Entity {
public:
    /// Construct an entity with its starting combat stats and position.
    /// @param kind          which concrete kind of entity this is (Player,
    ///                       MeleeEnemy, ...); stored so code can branch on the
    ///                       category without a dynamic_cast.
    /// @param position      the grid cell the entity starts on.
    /// @param health        the entity's starting Health; also used as the
    ///                       initial current Health.
    /// @param maxHealth     the most Health this entity can ever be healed up to.
    /// @param attack        the entity's base attack value (damage it deals).
    /// @param armor         the entity's damage reduction (subtracted from
    ///                       incoming attacks by the combat system).
    Entity(EntityKind kind, const Vec2& position, int health, int maxHealth,
           int attack, int armor);

    /// Virtual destructor so deleting a derived object through an `Entity*`
    /// runs the correct destructor (R4.7). Defaulted in Entity.cpp.
    virtual ~Entity();

    /// Report whether the entity is still in play.
    /// @return true while Health is strictly greater than 0; an entity is dead
    ///         once Health reaches or falls below 0 (R15.3 boundary).
    bool isAlive() const;

    /// Apply incoming damage, never letting Health drop below 0.
    /// @param amount the number of Health points to remove; values that would
    ///        push Health below 0 simply leave it at 0 (clamped low end).
    /// The result is clamped into [0, maxHealth], so Health can never become
    /// negative and can never exceed the maximum.
    void takeDamage(int amount);

    /// Restore Health, never letting it rise above the maximum.
    /// @param amount the number of Health points to add; any overflow past
    ///        maxHealth is discarded (clamped high end) (R19.1).
    /// The result is clamped into [0, maxHealth].
    void heal(int amount);

    /// @return the grid cell the entity currently occupies.
    Vec2 position() const;

    /// Move the entity to a new cell.
    /// @param newPosition the grid coordinate to place the entity on. Movement
    ///        legality (walls, bounds, occupancy) is the caller's concern; this
    ///        setter just records the new position.
    void setPosition(const Vec2& newPosition);

    /// @return the entity's current Health.
    int health() const;

    /// @return the entity's maximum Health (the heal ceiling).
    int maxHealth() const;

    /// @return the entity's attack value (damage dealt before target armor).
    int attack() const;

    /// @return the entity's current armor points (absorbs damage before HP).
    int armor() const;

    /// Reduce the entity's armor pool by the given amount, clamped to 0.
    /// Used by CombatSystem when incoming damage is absorbed by the armor
    /// buffer before reaching Health (armor-as-shield-buffer mechanic).
    /// @param amount the number of armor points to consume; the result is
    ///        clamped so armor_ never goes negative.
    void reduceArmor(int amount);

    /// Permanently raise both the maximum and the current Health of this
    /// entity by `amount`. Used by WaveManager to scale enemy survivability
    /// with the wave number so the player's mid-run upgrades (Extra Damage,
    /// Sharpened Blade, etc.) do not trivialise late waves. Negative or zero
    /// amounts are silently ignored. The current health is bumped by the same
    /// amount so a freshly spawned enemy at boosted maxHealth is also at full
    /// boosted health (rather than starting "wounded").
    /// @param amount the number of Health points to add to BOTH the cap and
    ///        the current value. Pass a positive integer.
    void boostMaxHealth(int amount);

    /// @return which concrete kind of entity this is (R4 polymorphism tag).
    EntityKind kind() const;

    /// The ASCII symbol used to draw this entity in the console renderer.
    /// Pure virtual, which is what makes Entity abstract: every concrete actor
    /// (Player '@', enemies 'M'/'R'/...) MUST supply its own glyph (R4).
    /// @return the single character that represents this entity on screen.
    virtual char glyph() const = 0;

protected:
    Vec2 position_;    ///< Current grid cell of the entity.
    int health_;       ///< Current Health; 0 means dead (R15.3).
    int maxHealth_;    ///< Upper bound that heal() can never exceed.
    int attack_;       ///< Base damage this entity deals on an attack.
    int armor_;        ///< Armor points; absorbs damage before HP (shield buffer).
    EntityKind kind_;  ///< Category tag for this concrete entity (R4).
};

} // namespace dga
