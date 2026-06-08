// =============================================================================
// core/Direction.h
//
// Purpose:
//   Defines the set of movement directions the game uses and the helpers that
//   convert a direction into a grid step (Vec2) and enumerate every direction.
//
//   Per locked decision OD-2, movement is 4-directional (orthogonal: up, down,
//   left, right). The helpers below are deliberately the single place that
//   knows "which way each direction points", so the rest of the code (BFS
//   pathfinding, enemy AI, Nova adjacency) iterates directions without ever
//   hardcoding offsets. The set can later be widened to 8 directions by editing
//   only this file.
//
// Why header-only:
//   Like Vec2, Direction is a trivial value type plus two tiny pure helpers, so
//   it lives entirely in this header. There is no Direction.cpp.
//
// Layer: core (depends only on core/Vec2.h).
// =============================================================================
#pragma once

#include <array>

#include "core/Vec2.h"

namespace dga {

/// The four orthogonal movement directions (OD-2: 4-directional movement).
///
/// Screen convention: y increases downward, so `Up` decreases the row index
/// and `Down` increases it. This matches how the grid is drawn top-to-bottom.
enum class Direction {
    Up,    ///< Toward smaller row index (y - 1).
    Down,  ///< Toward larger row index (y + 1).
    Left,  ///< Toward smaller column index (x - 1).
    Right  ///< Toward larger column index (x + 1).
};

/// The number of distinct movement directions (4 under OD-2).
/// Exposed as a named constant so callers never hardcode the literal 4.
constexpr int kDirectionCount = 4;

/// Convert a direction into the unit grid step it represents.
/// @param direction the direction to translate.
/// @return a Vec2 offset of length one in the given direction; for example
///         Direction::Right maps to Vec2{1, 0}. Returns Vec2{0, 0} only for an
///         unexpected value, which keeps callers safe against bad input.
inline Vec2 toOffset(Direction direction) {
    switch (direction) {
        case Direction::Up:    return Vec2(0, -1);
        case Direction::Down:  return Vec2(0, 1);
        case Direction::Left:  return Vec2(-1, 0);
        case Direction::Right: return Vec2(1, 0);
    }
    return Vec2(0, 0); // as default
}

/// Enumerate every movement direction in a fixed, deterministic order.
/// Used by BFS, enemy AI, and adjacency checks so they can loop over all
/// directions without knowing how many there are or where each one points.
/// @return an array containing each Direction exactly once.
inline std::array<Direction, kDirectionCount> allDirections() {
    return {Direction::Up, Direction::Down, Direction::Left, Direction::Right};
}

} // namespace dga
