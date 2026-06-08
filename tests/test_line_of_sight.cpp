// Feature: dungeon-grid-arena, Property 5: Line-of-sight correctness
//
// =============================================================================
// tests/test_line_of_sight.cpp
//
// Property 5 (design.md "Correctness Properties"):
//   "For any two tiles on a map, LineOfSight.hasLineOfSight returns true if and
//    only if no intermediate cell of the Bresenham line between them is a Wall,
//    and the result is symmetric (LOS(a,b) == LOS(b,a)) across horizontal,
//    vertical, diagonal, and oblique lines."
//   Validates: Requirements 13.1, 13.2, 13.3, 13.4
//
// This is a PROPERTY-BASED test: instead of a handful of fixed scenarios it
// drives many randomized maps and endpoint pairs through dga::LineOfSight and
// asserts the universal properties below on every one of them. All randomness
// flows through a single dga::Rng seeded with a fixed constant, so the whole
// test is deterministic and reproducible: rerunning it exercises the exact same
// sequence of maps and endpoints every time, which makes any failure debuggable.
//
// The properties verified, with the requirement each one anchors to:
//   * R13.1 (Bresenham line shape): lineCells(from, to) is a contiguous straight
//     line - it starts at `from`, ends at `to`, and every consecutive pair of
//     cells is a single grid step apart (Chebyshev distance exactly 1).
//   * R13.4 (symmetry / all orientations): the line and the visibility answer do
//     not depend on which endpoint is named first. lineCells(a, b) is the exact
//     reverse of lineCells(b, a), and hasLineOfSight(a, b) == hasLineOfSight(b, a)
//     for horizontal, vertical, diagonal, and oblique pairs alike.
//   * R13.2 / R13.3 (wall-blocking biconditional): hasLineOfSight is true if and
//     only if no INTERIOR cell of the line (every cell except the two endpoints)
//     is a Wall. We confirm this by INDEPENDENTLY scanning the interior cells in
//     the test and comparing our own verdict to the function's.
//   * Trivial visibility: identical or adjacent endpoints have no interior cells,
//     so they are always visible regardless of how many walls the map holds.
//
// The test #includes "doctest.h" WITHOUT the implementation macro; the doctest
// main() is provided once by tests/test_main.cpp.
// =============================================================================

#include <algorithm> // std::reverse - build the expected reversed line.
#include <vector>    // std::vector  - line-cell sequences.

#include "doctest.h"

#include "core/Enums.h"    // TileType { Floor, Wall }
#include "core/Rng.h"      // dga::Rng deterministic seeded generator
#include "core/Vec2.h"     // dga::Vec2 grid coordinate
#include "world/GridMap.h" // dga::GridMap (typeAt, setType, inBounds, width, height)
#include "world/LineOfSight.h" // dga::LineOfSight (lineCells, hasLineOfSight)

namespace {

using dga::GridMap;
using dga::LineOfSight;
using dga::Rng;
using dga::TileType;
using dga::Vec2;

// -----------------------------------------------------------------------------
// Test configuration.
// -----------------------------------------------------------------------------

// Minimum randomized iterations mandated by the design's PBT setup. We run more
// than the floor of 100 to widen coverage while staying fast.
constexpr int kIterations = 250;

// Fixed base seed: a constant seed makes the generated maps and endpoints - and
// therefore the entire test run - deterministic and reproducible.
constexpr unsigned int kBaseSeed = 0x10515E7u; // mnemonic: "LOS SET".

// Map dimension bounds. We keep maps small enough to be cheap but large enough
// (>= 3 on each side) that lines frequently have several interior cells, which
// is where wall blocking actually gets exercised.
constexpr int kMinDimension = 3;
constexpr int kMaxDimension = 16;

// Percentage chance (0..100) that any given non-border cell is carved as a Wall.
// A moderate density produces a healthy mix of blocked and clear lines so both
// arms of the biconditional (true and false outcomes) are tested.
constexpr int kWallPercentChance = 35;

// -----------------------------------------------------------------------------
// Generators (smart, constrained to the valid input space, all Rng-driven).
// -----------------------------------------------------------------------------

/// Build a random map whose every cell is independently Floor or Wall.
///
/// The endpoints we later choose may legitimately land on either tile type
/// (LineOfSight ignores the endpoints themselves), so we deliberately do NOT
/// reserve any cells as guaranteed-floor. The randomized wall density gives a
/// mixture of clear and obstructed lines across the run.
///
/// @param rng    the seeded generator driving all choices.
/// @param width  out: the chosen map width (columns).
/// @param height out: the chosen map height (rows).
/// @return a GridMap of the chosen size with randomized tile types.
GridMap makeRandomMap(Rng& rng, int& width, int& height) {
    width = rng.rangeInt(kMinDimension, kMaxDimension);
    height = rng.rangeInt(kMinDimension, kMaxDimension);

    GridMap map(width, height);
    // GridMap starts as all Wall; carve Floor / re-stamp Wall per cell at random.
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool wall = rng.rangeInt(1, 100) <= kWallPercentChance;
            map.setType(Vec2(x, y), wall ? TileType::Wall : TileType::Floor);
        }
    }
    return map;
}

/// Pick a uniformly random in-bounds cell of a `width` x `height` map.
/// @param rng    the seeded generator.
/// @param width  map width (columns); must be >= 1.
/// @param height map height (rows); must be >= 1.
/// @return a coordinate guaranteed to satisfy map.inBounds(...).
Vec2 randomCell(Rng& rng, int width, int height) {
    const int x = rng.rangeInt(0, width - 1);
    const int y = rng.rangeInt(0, height - 1);
    return Vec2(x, y);
}

// -----------------------------------------------------------------------------
// Helpers shared by the assertions.
// -----------------------------------------------------------------------------

/// Independently decide whether a line should be visible, using ONLY the cell
/// list and the map - never calling hasLineOfSight. This is the test's own
/// reference oracle for the wall-blocking biconditional (R13.2 / R13.3): a line
/// is clear exactly when none of its INTERIOR cells (all cells strictly between
/// the two endpoints) is a Wall.
/// @param map   the map whose tiles may obstruct the line.
/// @param cells the ordered Bresenham cells from `from` to `to`, inclusive.
/// @return true when no interior cell is a Wall.
bool interiorIsClear(const GridMap& map, const std::vector<Vec2>& cells) {
    // With two or fewer cells there are no interior cells (identical or adjacent
    // endpoints), so the line is trivially clear.
    if (cells.size() <= 2) {
        return true;
    }
    for (std::size_t index = 1; index + 1 < cells.size(); ++index) {
        if (map.typeAt(cells[index]) == TileType::Wall) {
            return false;
        }
    }
    return true;
}

} // namespace

// =============================================================================
// Property 5 - the single property-based test (one test per property).
// =============================================================================
TEST_CASE("Property 5: line-of-sight correctness (Bresenham shape, symmetry, "
          "wall-blocking biconditional)") {
    Rng rng(kBaseSeed); // Fixed seed => deterministic, reproducible run.

    for (int iteration = 0; iteration < kIterations; ++iteration) {
        // Per-iteration context so a failing case prints which iteration broke.
        INFO("iteration = " << iteration);

        int width = 0;
        int height = 0;
        const GridMap map = makeRandomMap(rng, width, height);

        const Vec2 a = randomCell(rng, width, height);
        const Vec2 b = randomCell(rng, width, height);
        INFO("a = (" << a.x << ", " << a.y << ")  b = (" << b.x << ", " << b.y
                     << ")  map = " << width << "x" << height);

        const std::vector<Vec2> forward = LineOfSight::lineCells(a, b);
        const std::vector<Vec2> backward = LineOfSight::lineCells(b, a);

        // --- R13.1: the cell list is a contiguous Bresenham line ---------------
        // It must be non-empty, begin exactly at `from`, and end exactly at `to`.
        REQUIRE_FALSE(forward.empty());
        CHECK(forward.front() == a);
        CHECK(forward.back() == b);

        // Every consecutive pair must be a single grid step apart: Chebyshev
        // distance exactly 1 means the cells touch (orthogonally or diagonally)
        // and are never equal or separated by a gap - i.e. a true contiguous line.
        for (std::size_t index = 1; index < forward.size(); ++index) {
            const int step = forward[index - 1].chebyshev(forward[index]);
            CHECK(step == 1);
        }

        // --- R13.4: symmetry of the line itself --------------------------------
        // lineCells(a, b) must be the EXACT reverse of lineCells(b, a). Build the
        // reversed forward line and compare element-by-element.
        std::vector<Vec2> reversedForward = forward;
        std::reverse(reversedForward.begin(), reversedForward.end());
        REQUIRE(reversedForward.size() == backward.size());
        for (std::size_t index = 0; index < backward.size(); ++index) {
            CHECK(reversedForward[index] == backward[index]);
        }

        // --- R13.4: symmetry of the visibility verdict -------------------------
        const bool losForward = LineOfSight::hasLineOfSight(map, a, b);
        const bool losBackward = LineOfSight::hasLineOfSight(map, b, a);
        CHECK(losForward == losBackward);

        // --- R13.2 / R13.3: wall-blocking biconditional ------------------------
        // Independently scan the interior cells and require hasLineOfSight to
        // agree: visible IFF no interior wall. This proves both directions of the
        // biconditional - a single interior wall blocks (R13.2); a wall-free
        // interior is visible (R13.3) - across whatever orientation this random
        // pair happened to produce (R13.4).
        const bool expectedVisible = interiorIsClear(map, forward);
        CHECK(losForward == expectedVisible);
    }
}

// =============================================================================
// Trivial-visibility property: identical or adjacent endpoints have no interior
// cells, so the target is ALWAYS visible no matter how many walls the map holds.
// Kept as a focused companion to Property 5's main biconditional check.
// =============================================================================
TEST_CASE("Property 5: identical and adjacent endpoints are always visible") {
    Rng rng(kBaseSeed ^ 0x9E3779B9u); // Distinct but still fixed/deterministic.

    for (int iteration = 0; iteration < kIterations; ++iteration) {
        INFO("iteration = " << iteration);

        int width = 0;
        int height = 0;
        const GridMap map = makeRandomMap(rng, width, height);

        const Vec2 origin = randomCell(rng, width, height);

        // Case 1: identical endpoints. lineCells is a single cell, so there are
        // no interior cells to block: visibility must hold even on a walled cell.
        {
            const std::vector<Vec2> cells = LineOfSight::lineCells(origin, origin);
            REQUIRE(cells.size() == 1);
            CHECK(cells.front() == origin);
            CHECK(LineOfSight::hasLineOfSight(map, origin, origin));
        }

        // Case 2: an adjacent endpoint (one of the 8 neighbours), when it stays
        // in bounds. Adjacent cells produce a two-cell line (the two endpoints
        // only), which again has no interior cell to obstruct it, so the line of
        // sight is always clear regardless of the tiles' types.
        const int dx = rng.rangeInt(-1, 1);
        const int dy = rng.rangeInt(-1, 1);
        const Vec2 neighbour(origin.x + dx, origin.y + dy);
        if ((dx != 0 || dy != 0) && map.inBounds(neighbour)) {
            INFO("neighbour = (" << neighbour.x << ", " << neighbour.y << ")");
            const std::vector<Vec2> cells =
                LineOfSight::lineCells(origin, neighbour);
            REQUIRE(cells.size() == 2);
            CHECK(cells.front() == origin);
            CHECK(cells.back() == neighbour);
            CHECK(LineOfSight::hasLineOfSight(map, origin, neighbour));
            // Symmetry holds for the adjacent case too.
            CHECK(LineOfSight::hasLineOfSight(map, neighbour, origin));
        }
    }
}
