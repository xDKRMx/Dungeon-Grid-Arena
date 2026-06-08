// =============================================================================
// world/Pathfinder.h
//
// Purpose:
//   Pathfinder answers the one question enemy AI keeps asking: "what is the
//   shortest walkable route from where I stand to the player, and what is the
//   very next tile I should step onto to get there?" It computes that route with
//   a breadth-first search (BFS) over the dungeon's Floor tiles (R12.1).
//
// Why BFS gives a SHORTEST path here (read this once):
//   On this grid every legal move costs exactly the same - one step to an
//   orthogonally adjacent Floor tile (OD-2, 4-directional). When all edges have
//   the same weight, breadth-first search visits tiles strictly in order of how
//   many steps they are from the start: first every tile 1 step away, then every
//   tile 2 steps away, and so on. So the FIRST time BFS reaches any tile, it has
//   reached it by a path of the fewest possible steps. The moment it reaches the
//   goal, that distance is therefore the true shortest distance (R12.1, R12.5).
//   (BFS only guarantees a shortest path because the steps are unweighted; if
//   different moves had different costs we would need Dijkstra's algorithm.)
//
// What counts as "blocked":
//   Walls are never walkable (R12.4 / R12.5). On top of that, the caller passes
//   a list of blockedTiles - tiles occupied by other living entities - which are
//   also treated as impassable, so an enemy routes AROUND its allies instead of
//   through them (R12.5). The goal tile (the player) is the target of the search
//   and is therefore exempt from the blocked check, matching the assumption in
//   R12.5 that "occupied floor tiles block movement but the destination Player
//   tile is the target".
//
// Why a .h/.cpp split:
//   The BFS carries real logic, so the declarations live here and the bodies
//   live in Pathfinder.cpp, following the project's multi-file rule (R2.1).
//
// Templates note (R7.2):
//   The search uses a local Grid<int> as its distance/visited buffer. That is a
//   SECOND, distinct instantiation of the Grid<T> template (the first is
//   Grid<Tile> inside GridMap), which is exactly what the templates rubric asks
//   for: one generic container reused for two different element types.
//
// Layer: world (depends on core/Vec2.h and world/GridMap.h).
// =============================================================================
#pragma once

#include <vector> // std::vector - the path result and the blockedTiles input.

#include "core/Vec2.h" // Vec2 grid coordinate

namespace dga {

class GridMap; // forward declaration: only referenced by const& below.

/// Stateless service that computes shortest walkable routes with BFS (R12).
///
/// The class holds no state of its own; each call allocates the buffers it needs
/// and throws them away when it returns. That makes a Pathfinder cheap to share
/// (pass it by const reference) and trivial to reason about and test.
class Pathfinder {
public:
    /// Compute the shortest walkable path from `start` to `goal`.
    ///
    /// The search is a breadth-first search over Floor tiles using 4-directional
    /// moves (OD-2). Walls and any coordinate listed in `blockedTiles` are
    /// treated as impassable (R12.4, R12.5); the `goal` tile is exempt from the
    /// blocked check because it is the search target (the player's tile).
    ///
    /// @param map          the dungeon grid to walk over.
    /// @param start        the tile the path begins on (e.g. the enemy's tile).
    /// @param goal         the tile the path should end on (e.g. the player).
    /// @param blockedTiles tiles occupied by other entities, treated as walls
    ///                     for this search (defaults to none).
    /// @return the shortest path as a list of coordinates FROM `start` TO `goal`
    ///         INCLUSIVE (so the first element is always `start` and the last is
    ///         always `goal`). Returns an EMPTY vector when no walkable path
    ///         exists, when `goal` is not a walkable Floor tile, or when either
    ///         endpoint is outside the map (R12.4). When `start == goal` the
    ///         path is a single element `{ start }`.
    std::vector<Vec2> findPath(const GridMap& map,
                               const Vec2& start,
                               const Vec2& goal,
                               const std::vector<Vec2>& blockedTiles = {}) const;

    /// Return just the FIRST step of the shortest path from `start` to `goal`.
    ///
    /// This is the convenience the enemy AI uses to chase the player one tile per
    /// turn: it runs the same BFS as findPath and hands back the single tile the
    /// entity should move onto next.
    ///
    /// @param map          the dungeon grid to walk over.
    /// @param start        the tile the mover currently occupies.
    /// @param goal         the tile to head toward (e.g. the player).
    /// @param blockedTiles tiles occupied by other entities (defaults to none).
    /// @return the tile adjacent to `start` that lies on the shortest path. When
    ///         there is NO path, or the mover is ALREADY at the goal, this
    ///         returns `start` itself - the caller reads "next step == start" as
    ///         "stay put / hold position" (R12.4). Returning `start` (rather than
    ///         a sentinel) keeps callers from ever stepping onto an invalid tile.
    Vec2 nextStepToward(const GridMap& map,
                        const Vec2& start,
                        const Vec2& goal,
                        const std::vector<Vec2>& blockedTiles = {}) const;
};

} // namespace dga
