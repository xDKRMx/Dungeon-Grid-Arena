// Feature: dungeon-grid-arena, Property 1: Generated map invariant
//
// =============================================================================
// tests/test_mapgen_invariant.cpp
//
// Property 1 (design.md > Correctness Properties):
//   "For any RNG seed and wave number, the map produced by MapGenerator.generate
//    has exactly the configured dimensions, contains only Floor or Wall tiles,
//    has every border cell as a Wall, and has all Floor tiles mutually reachable
//    by orthogonal movement."
//   Validates: Requirements 9.1, 9.2, 9.3, 9.5.
//
// This is a PROPERTY-BASED test: instead of a few hand-picked maps it checks the
// invariant across MANY randomized inputs - a wide range of grid sizes, enemy
// spawn counts, and RNG seeds - all driven by the project's own deterministic
// dga::Rng. Because every input is derived from a single fixed base seed, the
// whole test is reproducible: it explores the same inputs on every run, so a
// failure can always be reproduced and debugged.
//
// For each generated map we independently verify the four facets of Property 1
// plus the R9.5 "enough floors" guarantee and an R9 reproducibility check:
//
//   (a) DIMENSIONS (R9.1): generate() never resizes the map - the result still
//       has exactly the width/height the GridMap was constructed with.
//   (b) TILE DOMAIN (R9.1): every cell is either Floor or Wall, nothing else.
//   (c) SOLID BORDER (R9.3): every cell on the outer ring is a Wall, so no
//       entity can ever step outside the playable area.
//   (d) FULL CONNECTIVITY (R9.2): every Floor tile is reachable from every other
//       Floor tile by 4-directional (orthogonal) Floor movement. This is the
//       heart of the property and is verified with our OWN flood fill written
//       below - we deliberately do NOT trust MapGenerator::isFullyConnected to
//       prove its own output. (We do cross-check that our independent verdict
//       agrees with isFullyConnected, as a bonus consistency assertion.)
//   (e) ENOUGH FLOORS (R9.5): the map holds at least 1 + enemySpawnCount Floor
//       tiles (one for the player, one per enemy) - except on grids too small to
//       fit them, where R9.6 lets the generator fall back to the maximum number
//       of Floor tiles the interior can physically hold.
//   (f) DETERMINISM (R9 reproducibility): generating twice from the same seed
//       and inputs yields a byte-for-byte identical map.
//
// Framework: doctest (design.md "Property-Based Testing Setup"). This translation
// unit includes "doctest.h" WITHOUT the implementation macro; tests/test_main.cpp
// already defines doctest's main().
// =============================================================================
#include "doctest.h"

#include <queue>  // std::queue - the FIFO frontier for our independent flood fill.
#include <vector> // std::vector - the visited buffer for the flood fill.

#include "core/Enums.h"          // TileType { Floor, Wall }
#include "core/Rng.h"            // dga::Rng - deterministic seeded PRNG (input driver).
#include "core/Vec2.h"           // dga::Vec2 - grid coordinate.
#include "world/GridMap.h"       // dga::GridMap - width/height/typeAt/floorTiles.
#include "world/MapGenerator.h"  // dga::MapGenerator - generate/isFullyConnected.

namespace {

// -----------------------------------------------------------------------------
// Test configuration constants (kept named so the intent is self-documenting).
// -----------------------------------------------------------------------------

/// Fixed base seed for the input driver. A constant base seed makes the entire
/// property test deterministic and reproducible: every run explores the exact
/// same sequence of (width, height, spawnCount, mapSeed) tuples.
constexpr unsigned int kBaseSeed = 1234567u;

/// Number of randomized iterations. Property tests in this project run a MINIMUM
/// of 100 iterations; we use 200 to sample the input space more thoroughly.
constexpr int kIterations = 200;

/// Smallest and largest grid side we generate. The range starts at 1 on purpose
/// so the test also exercises degenerate grids that have no interior at all
/// (width or height <= 2), where the R9.6 fallback applies and the map is simply
/// all Wall. The upper bound keeps each generate()/flood-fill cheap.
constexpr int kMinGridSide = 1;
constexpr int kMaxGridSide = 16;

/// Range of enemy spawn counts requested per iteration. The upper bound is set
/// deliberately high relative to small grids so that some iterations cannot fit
/// "1 + spawnCount" floors, forcing and thereby exercising the R9.6 fallback.
constexpr int kMinSpawnCount = 0;
constexpr int kMaxSpawnCount = 15;

/// The player always occupies exactly one tile in addition to the enemy spawns.
/// Naming it keeps the "1 + spawnCount" floor arithmetic readable.
constexpr int kPlayerTileCount = 1;

// -----------------------------------------------------------------------------
// Independent orthogonal neighbour offsets.
//
// These are written out by hand in the TEST file (rather than reusing the
// production core/Direction.h helpers) so that our connectivity check shares NO
// code with the implementation under test. The four entries are the up, down,
// left, and right steps for 4-directional movement (OD-2).
// -----------------------------------------------------------------------------
constexpr int kNeighbourDx[4] = {0, 0, -1, 1};
constexpr int kNeighbourDy[4] = {-1, 1, 0, 0};

/// Count how many Floor tiles a map contains, by scanning every cell.
/// Independent of GridMap::floorTiles() so the floor-count invariant is checked
/// with the test's own measurement.
/// @param map the generated map to scan.
/// @return the number of cells whose TileType is Floor.
int countFloorTiles(const dga::GridMap& map) {
    int floorCount = 0;
    for (int y = 0; y < map.height(); ++y) {
        for (int x = 0; x < map.width(); ++x) {
            if (map.typeAt(dga::Vec2(x, y)) == dga::TileType::Floor) {
                ++floorCount;
            }
        }
    }
    return floorCount;
}

/// Verify that every cell on the outer ring of the map is a Wall (R9.3).
/// A border cell is any cell in the first/last row or first/last column. For a
/// degenerate 1-wide or 1-tall grid every cell is a border cell, which is still
/// correctly handled here.
/// @param map the generated map to inspect.
/// @return true when every border cell is a Wall tile.
bool borderIsAllWall(const dga::GridMap& map) {
    const int width = map.width();
    const int height = map.height();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool isBorder =
                (x == 0) || (y == 0) || (x == width - 1) || (y == height - 1);
            if (isBorder && map.typeAt(dga::Vec2(x, y)) != dga::TileType::Wall) {
                return false;
            }
        }
    }
    return true;
}

/// Verify that every cell is either Floor or Wall and nothing else (R9.1).
/// @param map the generated map to inspect.
/// @return true when every cell's TileType is one of the two valid kinds.
bool onlyFloorOrWall(const dga::GridMap& map) {
    for (int y = 0; y < map.height(); ++y) {
        for (int x = 0; x < map.width(); ++x) {
            const dga::TileType type = map.typeAt(dga::Vec2(x, y));
            if (type != dga::TileType::Floor && type != dga::TileType::Wall) {
                return false;
            }
        }
    }
    return true;
}

/// Independently decide whether ALL Floor tiles are mutually reachable by
/// orthogonal Floor movement (R9.2), using our own breadth-first flood fill.
///
/// Method:
///   1. Scan the grid to count the total number of Floor tiles and remember the
///      first one encountered. A map with 0 or 1 Floor tile is connected by
///      definition - there is nothing to be stranded from.
///   2. Flood-fill outward from that first Floor tile, spreading only to
///      orthogonally adjacent Floor cells, marking each as visited so it is
///      counted exactly once.
///   3. The map is fully connected exactly when the flood reaches every Floor
///      tile the scan found. If it reaches fewer, some Floor tiles are sealed
///      off in a separate pocket and the invariant is violated.
///
/// This shares no code with MapGenerator's internal flood fill, so it is an
/// honest, independent witness to connectivity rather than the implementation
/// vouching for itself.
///
/// @param map the generated map to test for full Floor connectivity.
/// @return true when all Floor tiles form a single connected region.
bool allFloorTilesConnected(const dga::GridMap& map) {
    const int width = map.width();
    const int height = map.height();

    // Step 1: count floors and find a starting cell.
    int totalFloorTiles = 0;
    dga::Vec2 startFloor(0, 0);
    bool haveStart = false;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (map.typeAt(dga::Vec2(x, y)) == dga::TileType::Floor) {
                ++totalFloorTiles;
                if (!haveStart) {
                    startFloor = dga::Vec2(x, y);
                    haveStart = true;
                }
            }
        }
    }
    if (totalFloorTiles <= 1) {
        return true; // Zero or one floor cannot be "disconnected".
    }

    // Step 2: BFS flood fill from the first Floor tile. visited[y][x] guards
    // against re-enqueuing a cell, so each Floor tile is counted once.
    std::vector<std::vector<bool>> visited(
        height, std::vector<bool>(width, false));
    std::queue<dga::Vec2> frontier;
    visited[startFloor.y][startFloor.x] = true;
    frontier.push(startFloor);

    int reachedFloorTiles = 0;
    while (!frontier.empty()) {
        const dga::Vec2 current = frontier.front();
        frontier.pop();
        ++reachedFloorTiles;

        for (int direction = 0; direction < 4; ++direction) {
            const int nx = current.x + kNeighbourDx[direction];
            const int ny = current.y + kNeighbourDy[direction];

            // Skip neighbours that fall off the grid.
            if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
                continue;
            }
            // Skip already-visited cells and any non-Floor cell.
            if (visited[ny][nx]) {
                continue;
            }
            if (map.typeAt(dga::Vec2(nx, ny)) != dga::TileType::Floor) {
                continue;
            }
            visited[ny][nx] = true;
            frontier.push(dga::Vec2(nx, ny));
        }
    }

    // Step 3: connected iff the flood reached every Floor tile.
    return reachedFloorTiles == totalFloorTiles;
}

/// Compare two maps cell-by-cell for an exact match (used by the determinism
/// check). Two maps are identical when they share dimensions and every cell has
/// the same TileType.
/// @param lhs first map.
/// @param rhs second map.
/// @return true when the two maps are byte-for-byte equivalent in tile content.
bool mapsIdentical(const dga::GridMap& lhs, const dga::GridMap& rhs) {
    if (lhs.width() != rhs.width() || lhs.height() != rhs.height()) {
        return false;
    }
    for (int y = 0; y < lhs.height(); ++y) {
        for (int x = 0; x < lhs.width(); ++x) {
            if (lhs.typeAt(dga::Vec2(x, y)) != rhs.typeAt(dga::Vec2(x, y))) {
                return false;
            }
        }
    }
    return true;
}

/// Compute the maximum number of Floor tiles a grid of the given size can hold,
/// which equals its interior area (the grid minus its solid Wall border). This
/// mirrors the generator's own "interior capacity" reasoning and is what the
/// R9.6 fallback caps the floor count at on grids too small to seat everyone.
/// @param width  grid width in tiles.
/// @param height grid height in tiles.
/// @return the interior cell count: (width-2) * (height-2), clamped to 0.
int interiorCapacity(int width, int height) {
    const int interiorWidth = (width > 2) ? width - 2 : 0;
    const int interiorHeight = (height > 2) ? height - 2 : 0;
    return interiorWidth * interiorHeight;
}

} // namespace

// =============================================================================
// The property test.
// =============================================================================
TEST_CASE("Property 1: generated map invariant holds across many seeds/sizes") {
    // The single deterministic driver that produces every randomized input.
    // Seeding it with a fixed base seed makes the whole test reproducible.
    dga::Rng inputDriver(kBaseSeed);

    // MapGenerator is a stateless service, so one instance generates every map.
    const dga::MapGenerator generator;

    for (int iteration = 0; iteration < kIterations; ++iteration) {
        // --- Draw this iteration's inputs from the deterministic driver. ------
        const int width = inputDriver.rangeInt(kMinGridSide, kMaxGridSide);
        const int height = inputDriver.rangeInt(kMinGridSide, kMaxGridSide);
        const int enemySpawnCount =
            inputDriver.rangeInt(kMinSpawnCount, kMaxSpawnCount);
        // A distinct map seed per iteration, varied off the fixed base seed.
        const unsigned int mapSeed =
            static_cast<unsigned int>(inputDriver.rangeInt(0, 1000000000));

        // Record the inputs so any failing assertion prints the exact example
        // needed to reproduce it.
        CAPTURE(iteration);
        CAPTURE(width);
        CAPTURE(height);
        CAPTURE(enemySpawnCount);
        CAPTURE(mapSeed);

        // --- Generate the map under test. ------------------------------------
        dga::Rng mapRng(mapSeed);
        dga::GridMap map(width, height);
        generator.generate(map, mapRng, enemySpawnCount);

        // (a) DIMENSIONS (R9.1): generate() must not resize the map.
        CHECK(map.width() == width);
        CHECK(map.height() == height);

        // (b) TILE DOMAIN (R9.1): only Floor or Wall tiles exist.
        CHECK(onlyFloorOrWall(map));

        // (c) SOLID BORDER (R9.3): every outer-ring cell is a Wall.
        CHECK(borderIsAllWall(map));

        // (d) FULL CONNECTIVITY (R9.2): verified by our OWN independent flood
        //     fill - we never ask the implementation to vouch for itself.
        const bool independentlyConnected = allFloorTilesConnected(map);
        CHECK(independentlyConnected);

        //     Bonus cross-check: our independent verdict must agree with the
        //     generator's own isFullyConnected() helper. If these ever diverge,
        //     one of the two flood fills is wrong and worth investigating.
        CHECK(independentlyConnected == generator.isFullyConnected(map));

        // (e) ENOUGH FLOORS (R9.5 / R9.6): the map holds at least
        //     1 + enemySpawnCount floors, or - on grids too small to fit them -
        //     the maximum the interior can hold (its capacity).
        const int requiredFloors = kPlayerTileCount + enemySpawnCount;
        const int capacity = interiorCapacity(width, height);
        const int feasibleFloors =
            (requiredFloors < capacity) ? requiredFloors : capacity;
        const int actualFloors = countFloorTiles(map);
        CHECK(actualFloors >= feasibleFloors);

        // (f) DETERMINISM (R9 reproducibility): regenerating from the same seed
        //     and inputs yields an identical map.
        dga::Rng mapRngRepeat(mapSeed);
        dga::GridMap mapRepeat(width, height);
        generator.generate(mapRepeat, mapRngRepeat, enemySpawnCount);
        CHECK(mapsIdentical(map, mapRepeat));
    }
}
