// =============================================================================
// world/LineOfSight.h
//
// Purpose:
//   LineOfSight answers a single tactical question for the rest of the game:
//   "standing on cell A, is there a clear straight shot to cell B, or is a Wall
//   in the way?" Ranged enemies use it to decide whether they may fire at the
//   Player (R13.1-R13.4), and the same straight-line walk is reused to fly a
//   Player projectile cell by cell (R16). Keeping both jobs in one tiny, stateless
//   helper means there is exactly one definition of "the cells on the line
//   between two points," so visibility and projectiles can never disagree.
//
//   The cells along that line are produced with Bresenham's line algorithm. In
//   plain language, Bresenham draws the straight line between two grid cells
//   using only integer addition - no floating-point, no division - by walking one
//   step at a time along whichever axis the line travels farthest on (the "major"
//   axis) and keeping a running error term that tells it when to also take a step
//   on the other ("minor") axis. Each loop it asks "have I drifted far enough off
//   the ideal line to nudge onto the next row/column yet?"; the integer error term
//   answers that without ever leaving whole numbers. The result is the set of grid
//   cells the ideal straight line passes through, which is exactly what a sight
//   line or a projectile should sweep.
//
// Endpoint / blocking policy (read once, applies to everything below):
//   * lineCells(from, to) returns the cells in order and INCLUDES BOTH endpoints:
//     the first element is always `from` and the last is always `to`.
//   * hasLineOfSight only treats INTERMEDIATE cells as possible blockers. The two
//     endpoints never block: you can always see the cell you are standing on, and
//     you can always see the target cell itself (a wall standing *on* the target
//     does not hide the target) (R13.2). A Wall strictly between the two endpoints
//     breaks the line of sight.
//
// Why static, stateless functions:
//   Both operations are pure functions of their inputs - they read no object
//   state and store nothing between calls - so they are exposed as `static`
//   members and called as LineOfSight::lineCells(...) / hasLineOfSight(...). This
//   matches the design's "stateless service" intent and lets the CombatSystem
//   reuse lineCells without constructing a throwaway object.
//
// Why a .h/.cpp split:
//   The Bresenham walk is real, non-trivial logic, so the declarations live here
//   and the bodies live in LineOfSight.cpp, per the project's multi-file rule.
//
// Layer: world (depends on core/Vec2.h; uses world/GridMap.h only in the .cpp).
// =============================================================================
#pragma once

#include <vector> // std::vector - return type of lineCells().

#include "core/Vec2.h" // Vec2 grid coordinate / offset.

namespace dga {

// Forward declaration: hasLineOfSight only needs a reference to a GridMap, so the
// header does not need GridMap's full definition. The .cpp includes the real
// header to call typeAt(). This keeps the world layer's compile-time coupling low.
class GridMap;

/// Stateless helper that turns two grid coordinates into the straight line of
/// cells between them and reports whether that line is unobstructed.
///
/// The class holds no data members; every operation is a `static` pure function.
/// It is the project's Bresenham showcase (R13).
class LineOfSight {
public:
    /// Compute the grid cells along the straight line from `from` to `to`.
    ///
    /// Uses Bresenham's integer line algorithm (see the file header for the plain
    /// language explanation). The returned sequence is ordered and INCLUDES BOTH
    /// ENDPOINTS: result.front() == from and result.back() == to. When from == to
    /// the result is a single-cell vector containing just that cell.
    ///
    /// Symmetry guarantee: lineCells(a, b) is exactly the reverse of
    /// lineCells(b, a). The two calls therefore visit the identical set of cells,
    /// which is what lets hasLineOfSight give the same answer in both directions
    /// (LOS(a, b) == LOS(b, a)). This is achieved by always running the walk in a
    /// fixed canonical orientation and flipping the output to match the requested
    /// direction (see LineOfSight.cpp).
    ///
    /// @param from the starting cell (included as the first element).
    /// @param to   the ending cell (included as the last element).
    /// @return the cells the straight line passes through, in order from `from`
    ///         to `to`. This is reused both for line-of-sight checks and for
    ///         ranged projectile travel (R13, R16).
    static std::vector<Vec2> lineCells(const Vec2& from, const Vec2& to);

    /// Report whether `to` is visible from `from` across the given map.
    ///
    /// Walks lineCells(from, to) and inspects only the INTERMEDIATE cells (every
    /// cell except the two endpoints). If any intermediate cell is a Wall the line
    /// is blocked and this returns false (R13.2); otherwise the target is visible
    /// and it returns true (R13.3). The endpoints themselves never block: you can
    /// always see your own cell and the target cell, even if a wall sits on the
    /// target. from == to is trivially visible, and adjacent cells are always
    /// visible (there are no intermediate cells to block them).
    ///
    /// Works for horizontal, vertical, diagonal, and oblique lines alike (R13.4),
    /// because Bresenham handles every direction with the same integer walk.
    ///
    /// @param map  the dungeon whose Wall tiles can obstruct the line.
    /// @param from the observer's cell.
    /// @param to   the target cell being checked for visibility.
    /// @return true when no intermediate Wall lies between `from` and `to`.
    static bool hasLineOfSight(const GridMap& map, const Vec2& from,
                               const Vec2& to);
};

} // namespace dga
