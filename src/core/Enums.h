// =============================================================================
// core/Enums.h
//
// Purpose:
//   Central home for the small shared enumerations used across the whole game.
//   Centralizing them here keeps every module speaking the same vocabulary,
//   prevents individual modules from inventing their own "magic number" codes,
//   and gives save/load a single stable set of tags to serialize.
//
//   All enumerations are scoped (`enum class`) so their members never leak into
//   the surrounding namespace and cannot be implicitly converted to int by
//   accident. This makes the code safer and more readable.
//
// Why header-only:
//   These are pure type declarations with no associated logic, so there is no
//   Enums.cpp.
//
// Layer: core (depends on nothing).
// =============================================================================
#pragma once

namespace dga {

/// The kind of a single dungeon tile (R9.1).
/// Only two tile types exist: walkable floor and impassable wall.
enum class TileType {
    Floor, ///< Walkable ground that entities and projectiles can pass over.
    Wall   ///< Impassable barrier that blocks movement and line of sight.
};

/// Identifies which concrete kind of entity an Entity base pointer refers to.
/// Used for save/load tagging and for renderers/UI that need the category
/// without performing a dynamic_cast.
enum class EntityKind {
    Player,      ///< The single hero controlled by the user.
    MeleeEnemy,  ///< Attacks only when orthogonally adjacent (R14.1).
    RookEnemy,   ///< Fires along a shared row or column (R14.2).
    BishopEnemy, ///< Fires along a shared diagonal (R14.3).
    QueenEnemy,  ///< Fires along row, column, or diagonal (R14.4).
    FastEnemy,   ///< Moves up to two tiles per step (R12.3).
    BossEnemy    ///< Multi-phase boss that summons minions (R18).
};

/// Identifies which concrete kind of collectible an Item base pointer refers to.
/// Drives both virtual-effect dispatch verification and save/load tagging.
enum class ItemKind {
    HealthPotion, ///< Restores Health up to the maximum (R19.1).
    Weapon,       ///< Changes attack value / ranged capability (R19.2).
    AmmoItem,     ///< Adds ammunition for ranged weapons (R19.3).
    Armor,        ///< Increases armor damage reduction (R19.4).
    Treasure      ///< Adds value to the Score (R19.5).
};

/// Identifies which concrete player ability is being referenced.
/// Used by the ability system to look up and activate abilities by kind.
enum class AbilityKind {
    Dash,   ///< Move several tiles along floor in a direction (R21.5).
    Nova,   ///< Ultimate: adjacent area damage, consumes the Charge_Meter (R22).
    Shield, ///< Grants temporary damage immunity (R21.6).
    Blink   ///< Teleport to a visible empty floor tile (R21.7).
};

/// The high-level states managed by the game state machine (R27, R30).
/// Exactly one of these is active at a time.
enum class GameStateId {
    MainMenu,     ///< Title screen: start, load, high scores, quit.
    Playing,      ///< Active turn-based gameplay.
    Paused,       ///< Turn loop halted; save/load/controls/quit menu (R27).
    UpgradeDraft, ///< Between-wave card selection (R23).
    GameOver      ///< Run ended; prompt for name and record score (R25).
};

} // namespace dga
