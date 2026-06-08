// =============================================================================
// world/LineOfSight.cpp
//
// Purpose:
//   Definitions for the LineOfSight helpers declared in world/LineOfSight.h.
//   This file contains the actual Bresenham line walk and the wall-checking
//   visibility test built on top of it.
//
// Layer: world (depends on core/Vec2.h, core/Enums.h, world/GridMap.h).
// =============================================================================
#include "world/LineOfSight.h"

#include <algorithm> // std::reverse - flips a canonical walk to the requested order.
#include <cstdlib>   // std::abs    - axis distances are absolute values.

#include "core/Enums.h"  // TileType (Floor / Wall)
#include "world/GridMap.h" // GridMap::typeAt - reads tiles along the line.

namespace dga {

namespace {

// How many cells the canonical orientation advances per step on each axis.
// Bresenham walks one cell at a time, so a single step is always +/- one cell;
// naming the value avoids a bare "1" literal in the stepping logic below.
constexpr int kSingleCellStep = 1;

// The error-term comparisons below scale the accumulated error by two. This is
// the classic integer-only Bresenham trick: doubling lets the test use the raw
// dx/dy deltas instead of halves, so the whole decision stays in integers.
constexpr int kErrorScale = 2;

/// Decide whether one endpoint comes "before" another in a fixed canonical
/// order. Ordering is by column first, then by row. The order itself is
/// arbitrary; all that matters is that it is total and stable, because it lets
/// lineCells run the walk from the same end regardless of which way the caller
/// asked - the foundation of the symmetry guarantee (LOS(a, b) == LOS(b, a)).
/// @param a first cell.
/// @param b second cell.
/// @return true when `a` should be treated as the canonical start.
bool isCanonicalStart(const Vec2& a, const Vec2& b) {
    if (a.x != b.x) {
        return a.x < b.x;
    }
    return a.y <= b.y;
}

/// Walk Bresenham's line from `start` to `end`, inclusive of both, returning the
/// grid cells in order.
///
/// Plain-language recap of the algorithm (full version in the header):
///   * dx and dy are how far the line travels on each axis (absolute values).
///   * stepX / stepY are the direction (+1 or -1) to move on each axis.
///   * `error` accumulates how far the path has drifted from the ideal line. We
///     always advance along the major axis (the one with the larger delta); each
///     step we add that axis's delta to the error, and whenever the error grows
///     past the other axis's delta we also take one step on the minor axis and
///     pay the error back down. Doing it this way keeps the drawn cells hugging
///     the true straight line using integer math only.
/// @param start the first cell emitted.
/// @param end   the last cell emitted.
/// @return the cells from `start` to `end` in order, both endpoints included.
std::vector<Vec2> bresenhamWalk(const Vec2& start, const Vec2& end) {
    const int dx = std::abs(end.x - start.x);
    const int dy = std::abs(end.y - start.y);
    const int stepX = (start.x < end.x) ? kSingleCellStep : -kSingleCellStep;
    const int stepY = (start.y < end.y) ? kSingleCellStep : -kSingleCellStep;

    // The running error starts as the difference of the two axis deltas. A
    // positive bias means "the X axis is currently the one pulling us along."
    int error = dx - dy;

    int currentX = start.x;
    int currentY = start.y;

    std::vector<Vec2> cells;
    // We will emit dx (or dy, whichever is larger) + 1 cells; reserving that many
    // up front avoids repeated reallocation as the line grows.
    const int longestAxis = (dx > dy) ? dx : dy;
    cells.reserve(static_cast<std::size_t>(longestAxis) + 1);

    // Loop until we have placed the end cell. Because we emit the current cell at
    // the top of each iteration and only then step, both endpoints are included.
    while (true) {
        cells.push_back(Vec2(currentX, currentY));
        if (currentX == end.x && currentY == end.y) {
            break;
        }

        // Double the error so both comparisons can use the raw deltas (integers).
        const int doubledError = kErrorScale * error;

        // If we have drifted far enough horizontally, take a step on X and pay
        // down the error by the vertical delta.
        if (doubledError > -dy) {
            error -= dy;
            currentX += stepX;
        }
        // If we have drifted far enough vertically, take a step on Y and add the
        // horizontal delta back into the error. On a pure diagonal both branches
        // fire, stepping one cell on each axis at once.
        if (doubledError < dx) {
            error += dx;
            currentY += stepY;
        }
    }

    return cells;
}

} // namespace

// Public entry point. To guarantee that lineCells(a, b) is the exact reverse of
// lineCells(b, a), we always run the Bresenham walk from the canonical endpoint
// (chosen by isCanonicalStart) and then flip the result if the caller asked for
// the opposite direction. Running from the same physical end every time means
// the two directions visit the identical cells, never differing because of how
// Bresenham breaks ties.
std::vector<Vec2> LineOfSight::lineCells(const Vec2& from, const Vec2& to) {
    if (isCanonicalStart(from, to)) {
        // `from` is already the canonical start: walk straight from `from` to `to`.
        return bresenhamWalk(from, to);
    }

    // `to` is the canonical start: walk that canonical line, then reverse it so
    // the sequence still reads from the caller's `from` to their `to`.
    std::vector<Vec2> cells = bresenhamWalk(to, from);
    std::reverse(cells.begin(), cells.end());
    return cells;
}

// Visibility test. The endpoints never obstruct the view (you can see your own
// cell and the target cell), so we inspect only the interior cells of the line.
// A single Wall among those interior cells is enough to break line of sight.
bool LineOfSight::hasLineOfSight(const GridMap& map, const Vec2& from,
                                 const Vec2& to) {
    const std::vector<Vec2> cells = lineCells(from, to);

    // With two or fewer cells there are no intermediate cells to block the line:
    // this covers from == to (a single cell) and adjacent cells (two cells), both
    // of which are always visible.
    if (cells.size() <= 2) {
        return true;
    }

    // Skip index 0 (from) and the last index (to); only the cells strictly
    // between the endpoints can obstruct the line (R13.2).
    const std::size_t firstInterior = 1;
    const std::size_t lastInterior = cells.size() - 1; // one past the last interior
    for (std::size_t index = firstInterior; index < lastInterior; ++index) {
        if (map.typeAt(cells[index]) == TileType::Wall) {
            return false; // A wall between the endpoints hides the target (R13.2).
        }
    }

    return true; // No intermediate wall: the target is visible (R13.3).
}

} // namespace dga
