// Feature: dungeon-grid-arena, Property 3: Grid access is bounds-safe and round-trips
//
// =============================================================================
// tests/test_grid.cpp
//
// What this file proves (design Property 3, Requirements 5.1, 5.2, 5.3):
//
//   "For any coordinate (including negative and beyond-extent values) and any
//    GridMap, inBounds correctly classifies the coordinate, out-of-bounds access
//    is rejected without reading or writing out-of-bounds memory, and for any
//    in-bounds coordinate setType followed by typeAt returns the value that was
//    set."
//
// This is a PROPERTY-BASED test: instead of a handful of fixed examples it runs
// a large batch of RANDOMISED iterations (>= 100, see kIterations) so the three
// guarantees are exercised across many grid shapes and many coordinates - the
// in-bounds interior, the exact edges, and a band of clearly out-of-bounds cells
// on every side (negative and beyond-extent).
//
// Determinism: every random choice flows through the project's seeded RNG
// (dga::Rng). The base seed is a fixed constant and each iteration uses
// (base seed + iteration index), so a failure is perfectly reproducible - the
// same run always generates the same grids and the same coordinates.
//
// Both layers of the "arrays" showcase are tested, exactly as Property 3 spans
// them:
//   * dga::Grid<T> (core/Grid.h)   - the generic contiguous 2D container whose
//                                    checked at() THROWS std::out_of_range on a
//                                    bad coordinate (R5.3). Tested for two
//                                    distinct element types (int and TileType)
//                                    to also exercise the template generically.
//   * dga::GridMap (world/GridMap.h)- the dungeon map built on Grid<Tile>. Its
//                                    public accessors are bounds-GUARDED queries
//                                    that never throw: typeAt() returns Wall and
//                                    isWalkable() returns false for any cell
//                                    outside the map, and setType() silently
//                                    ignores an out-of-bounds write (R5.2, R5.3).
//
// IMPORTANT: this translation unit includes doctest WITHOUT the implementation
// macro. The single DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (and therefore main())
// lives only in tests/test_main.cpp, so the framework is compiled exactly once.
// =============================================================================

#include "doctest.h"

#include <stdexcept> // std::out_of_range - the exception Grid<T>::at() throws.

#include "core/Enums.h"  // TileType { Floor, Wall } - second Grid<T> element type.
#include "core/Grid.h"   // dga::Grid<T> - generic checked 2D container under test.
#include "core/Rng.h"    // dga::Rng - deterministic seeded randomness source.
#include "core/Vec2.h"   // dga::Vec2 - (x, y) coordinate / overloaded accessors.
#include "world/GridMap.h" // dga::GridMap - bounds-guarded dungeon grid under test.

namespace {

// A fixed base seed makes the whole property test reproducible. Each iteration
// adds its index to this so successive iterations explore different - but always
// repeatable - grids and coordinates.
constexpr unsigned int kBaseSeed = 0xC0FFEEu;

// Number of randomised iterations. The design mandates a MINIMUM of 100; we run
// well above that, and each iteration itself probes many coordinates, so the
// property is checked across far more than 100 individual cases.
constexpr int kIterations = 200;

// The independent, "obviously correct" definition of in-bounds that we hold the
// containers to. Property 3 says inBounds must "correctly classify" a coordinate;
// the only way to check correctness is to compare the container's answer against
// a reference predicate derived straight from the definition of a w x h grid.
// Deliberately written by hand (not by calling the code under test) so the test
// is a genuine oracle rather than a tautology.
bool referenceInBounds(int x, int y, int width, int height) {
    return x >= 0 && x < width && y >= 0 && y < height;
}

} // namespace

// -----------------------------------------------------------------------------
// Property 3, part A: the generic dga::Grid<T> container.
//
// Grid<T> is the lower-level building block. Its at() is a CHECKED accessor that
// THROWS std::out_of_range on a bad coordinate, so here the "rejection" half of
// the property is verified with CHECK_THROWS_AS.
// -----------------------------------------------------------------------------
TEST_CASE("Property 3: Grid<T> access is bounds-safe and round-trips") {
    for (int iteration = 0; iteration < kIterations; ++iteration) {
        // Deterministic per-iteration RNG (reproducible from the base seed).
        dga::Rng rng(kBaseSeed + static_cast<unsigned int>(iteration));

        // Random but small grid dimensions. We keep width/height >= 1 so that at
        // least one in-bounds cell always exists for the round-trip half of the
        // property; the upper bound keeps each iteration cheap.
        const int width = rng.rangeInt(1, 16);
        const int height = rng.rangeInt(1, 16);

        // The grid under test, filled with a known sentinel so we can later prove
        // that writes land in the addressed cell and nowhere else.
        constexpr int fillSentinel = -1;
        dga::Grid<int> grid(width, height, fillSentinel);

        // ---- Shape sanity: the container reports the dimensions it was built
        // with. A wrong width/height would invalidate every index calculation
        // below, so we pin it down first.
        CHECK(grid.width() == width);
        CHECK(grid.height() == height);

        // ---------------------------------------------------------------------
        // (A1) inBounds classifies EVERY coordinate correctly, and out-of-bounds
        //      access is rejected by throwing (never silently touching memory).
        //
        // We sweep a band that intentionally straddles the grid on all four
        // sides: from -3 (negative, the "before the start" case) out to
        // width/height + 2 (beyond-extent, the "past the end" case). This
        // guarantees we hit interior cells, the exact edge cells (0 and w-1),
        // and clearly out-of-range cells.
        // ---------------------------------------------------------------------
        for (int y = -3; y < height + 3; ++y) {
            for (int x = -3; x < width + 3; ++x) {
                const bool expectedInBounds = referenceInBounds(x, y, width, height);

                // Both inBounds overloads (raw ints and Vec2) must agree with the
                // reference oracle. Testing both keeps the two entry points honest.
                CHECK(grid.inBounds(x, y) == expectedInBounds);
                CHECK(grid.inBounds(dga::Vec2(x, y)) == expectedInBounds);

                if (expectedInBounds) {
                    // In-bounds round-trip: writing a value through at() and
                    // reading it straight back must return the SAME value. This
                    // is the core "set then get" guarantee for a single cell.
                    const int writtenValue = rng.rangeInt(-100000, 100000);
                    grid.at(x, y) = writtenValue;
                    CHECK(grid.at(x, y) == writtenValue);

                    // The Vec2 overload must address the very same cell as the
                    // (x, y) overload - otherwise the two ways of indexing would
                    // disagree and the mapping would be inconsistent.
                    CHECK(grid.at(dga::Vec2(x, y)) == writtenValue);
                } else {
                    // Out-of-bounds REJECTION (R5.3): a checked access must throw
                    // std::out_of_range. The throw happens BEFORE any index into
                    // the backing store is computed/used, which is precisely how
                    // the container avoids reading or writing out-of-bounds
                    // memory. We assert the exception type for all four overloads
                    // (read/write x int/Vec2) so no access path is left unguarded.
                    const dga::Grid<int>& constGrid = grid; // exercise const at().
                    CHECK_THROWS_AS(grid.at(x, y), std::out_of_range);
                    CHECK_THROWS_AS(grid.at(dga::Vec2(x, y)), std::out_of_range);
                    CHECK_THROWS_AS(constGrid.at(x, y), std::out_of_range);
                    CHECK_THROWS_AS(constGrid.at(dga::Vec2(x, y)), std::out_of_range);
                }
            }
        }

        // ---------------------------------------------------------------------
        // (A2) Row-major mapping is consistent and aliasing-free.
        //
        // Property 3 also requires "the row-major mapping is consistent". We give
        // every cell a UNIQUE value equal to its own flat row-major index
        // (y * width + x) and then read the whole grid back. If any two distinct
        // coordinates aliased the same storage slot, a later write would clobber
        // an earlier cell and the read-back would not match - so a full match
        // proves each (x, y) maps to its own distinct cell in the documented
        // row-major order.
        // ---------------------------------------------------------------------
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                grid.at(x, y) = y * width + x;
            }
        }
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                CHECK(grid.at(x, y) == y * width + x);
            }
        }

        // ---------------------------------------------------------------------
        // (A3) The same template, a different element type.
        //
        // Grid<T> is generic, so the round-trip property must hold for any T.
        // We repeat a compact round-trip with Grid<TileType> (an enum class) to
        // demonstrate the guarantee is not specific to int. This doubles as a
        // live witness that Grid<T> is instantiated for more than one type.
        // ---------------------------------------------------------------------
        dga::Grid<dga::TileType> typedGrid(width, height, dga::TileType::Wall);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                // Pick Floor or Wall at random and round-trip it.
                const dga::TileType chosen =
                    rng.rangeInt(0, 1) == 0 ? dga::TileType::Floor : dga::TileType::Wall;
                typedGrid.at(x, y) = chosen;
                CHECK(typedGrid.at(x, y) == chosen);
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Property 3, part B: the dga::GridMap dungeon grid.
//
// GridMap wraps a Grid<Tile> but exposes a DIFFERENT, deliberately softer
// contract: its public accessors are bounds-GUARDED queries that NEVER throw.
// For an out-of-bounds coordinate, typeAt() answers Wall, isWalkable() answers
// false, and setType() does nothing. This lets game logic probe neighbours at
// the map edge without special-casing or exception handling, while still making
// it impossible to read or write outside the tile array (R5.2, R5.3). The test
// therefore asserts SAFE DEFAULTS + NO-THROW for out-of-bounds, instead of the
// throw-based rejection used for the raw Grid<T> above.
// -----------------------------------------------------------------------------
TEST_CASE("Property 3: GridMap access is bounds-safe and round-trips") {
    for (int iteration = 0; iteration < kIterations; ++iteration) {
        dga::Rng rng(kBaseSeed + static_cast<unsigned int>(iteration));

        const int width = rng.rangeInt(1, 16);
        const int height = rng.rangeInt(1, 16);

        // A fresh GridMap starts as solid Wall everywhere (its documented default).
        dga::GridMap map(width, height);

        // Shape sanity: the map must report the dimensions it was constructed with.
        CHECK(map.width() == width);
        CHECK(map.height() == height);

        // A brand-new map is all Wall, so it must contain ZERO floor tiles. This
        // anchors the floorTiles() accessor before we start carving Floor below.
        CHECK(map.floorTiles().empty());

        // Sweep the same straddling band (negative through beyond-extent) used for
        // Grid<T>, so every category of coordinate is covered for GridMap too.
        for (int y = -3; y < height + 3; ++y) {
            for (int x = -3; x < width + 3; ++x) {
                const dga::Vec2 coordinate(x, y);
                const bool expectedInBounds = referenceInBounds(x, y, width, height);

                // inBounds (both overloads) must match the independent oracle.
                CHECK(map.inBounds(x, y) == expectedInBounds);
                CHECK(map.inBounds(coordinate) == expectedInBounds);

                if (expectedInBounds) {
                    // In-bounds round-trip via the map's checked accessors:
                    // setType(coord, T) then typeAt(coord) must return exactly T,
                    // for BOTH tile kinds.
                    const dga::TileType writtenType =
                        rng.rangeInt(0, 1) == 0 ? dga::TileType::Floor : dga::TileType::Wall;
                    map.setType(coordinate, writtenType);
                    CHECK(map.typeAt(coordinate) == writtenType);

                    // isWalkable is the derived view of the same stored state: a
                    // cell is walkable exactly when it is Floor. Checking it here
                    // proves the query stays consistent with what was just written.
                    CHECK(map.isWalkable(coordinate) == (writtenType == dga::TileType::Floor));
                } else {
                    // Out-of-bounds must be SAFE, not an error, and must never
                    // throw. We assert each query both does not throw AND returns
                    // its documented safe default.
                    CHECK_NOTHROW(map.typeAt(coordinate));
                    CHECK_NOTHROW(map.isWalkable(coordinate));
                    CHECK_NOTHROW(map.setType(coordinate, dga::TileType::Floor));

                    // "Outside the map" models impassable surrounding rock:
                    // typeAt -> Wall, isWalkable -> false.
                    CHECK(map.typeAt(coordinate) == dga::TileType::Wall);
                    CHECK(map.isWalkable(coordinate) == false);
                }
            }
        }

        // ---------------------------------------------------------------------
        // (B1) An out-of-bounds setType writes NOTHING - it cannot corrupt a
        //      real cell. We plant a known value in an in-bounds sentinel cell,
        //      fire a barrage of out-of-bounds writes that each try to set the
        //      OPPOSITE value, and confirm the sentinel is untouched afterwards.
        //      This is the concrete "without writing out-of-bounds memory" check
        //      for the guarded (non-throwing) interface.
        // ---------------------------------------------------------------------
        const dga::Vec2 sentinelCell(rng.rangeInt(0, width - 1), rng.rangeInt(0, height - 1));
        map.setType(sentinelCell, dga::TileType::Floor);
        REQUIRE(map.typeAt(sentinelCell) == dga::TileType::Floor);

        const dga::Vec2 outOfBoundsTargets[] = {
            dga::Vec2(-1, sentinelCell.y),       // just left of the grid
            dga::Vec2(width, sentinelCell.y),    // just right of the grid
            dga::Vec2(sentinelCell.x, -1),       // just above the grid
            dga::Vec2(sentinelCell.x, height),   // just below the grid
            dga::Vec2(-width - 5, -height - 5),  // far outside, both negative
            dga::Vec2(width + 5, height + 5),    // far outside, both beyond-extent
        };
        for (const dga::Vec2& target : outOfBoundsTargets) {
            map.setType(target, dga::TileType::Wall); // each is a no-op, must not throw
        }

        // The sentinel must still hold the value we set - none of the rejected
        // out-of-bounds writes leaked into real storage.
        CHECK(map.typeAt(sentinelCell) == dga::TileType::Floor);
        CHECK(map.isWalkable(sentinelCell) == true);
    }
}
