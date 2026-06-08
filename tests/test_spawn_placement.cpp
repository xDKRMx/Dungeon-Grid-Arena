// Feature: dungeon-grid-arena, Property 2: Spawn placement is valid and distinct
// =============================================================================
// tests/test_spawn_placement.cpp
//
// Property-based test for MapGenerator::pickSpawns (Property 2 in design.md):
//
//   "For any RNG seed and requested spawn count, the player tile and all enemy
//    spawn tiles returned for a generated map are Floor tiles, are pairwise
//    distinct, and exclude one another."
//   Validates: Requirements 9.4, 9.6.
//
// Strategy (in-house PBT, per design "Property-Based Testing Setup"):
//   doctest is the framework, and randomized inputs are produced by the project's
//   own deterministic, seeded dga::Rng rather than a third-party PBT library. A
//   single master Rng with a fixed seed drives every generated input, so the whole
//   test is reproducible: rerunning it explores the exact same sequence of cases.
//   We run well over the required minimum of 100 iterations.
//
//   This translation unit deliberately does NOT define the doctest implementation
//   macro: tests/test_main.cpp already provides doctest's main().
// =============================================================================
#include "doctest.h"

#include <algorithm> // std::min - expected enemy-count formula (R9.6).
#include <cstddef>   // std::size_t - indexing the spawn vector.
#include <vector>    // std::vector - floor-tile and spawn collections.

#include "core/Enums.h"          // TileType (Floor / Wall).
#include "core/Rng.h"            // dga::Rng - the deterministic randomness source.
#include "core/Vec2.h"           // dga::Vec2 - grid coordinate / equality.
#include "world/GridMap.h"       // dga::GridMap - floorTiles / typeAt / inBounds.
#include "world/MapGenerator.h"  // dga::MapGenerator / dga::SpawnPlan.

using namespace dga;

namespace {

// -----------------------------------------------------------------------------
// Test parameters (named constants, no magic numbers).
// -----------------------------------------------------------------------------

/// Number of randomized cases to explore. Comfortably above the design's minimum
/// of 100 iterations while staying fast (each map is at most ~22x22 cells).
constexpr int kIterations = 200;

/// Fixed master seed so the whole property test is deterministic / reproducible:
/// the same 200 (width, height, enemyCount, caseSeed) tuples are explored on
/// every run, which is what makes a failing example stable enough to debug.
constexpr unsigned int kMasterSeed = 0x5A9D2C7Bu;

/// Smallest grid dimension we generate. A 4x4 grid has a 2x2 interior, so the
/// generator always leaves at least one Floor tile to seat the Player on.
constexpr int kMinDimension = 4;

/// Largest grid dimension we generate. Keeps maps small enough to run hundreds of
/// generations quickly while still giving plenty of size variety.
constexpr int kMaxDimension = 22;

/// Largest enemy spawn count we request. Chosen well above the floor capacity of
/// the smallest grids so that the R9.6 "reduce the spawn count" path is exercised
/// frequently (small map + big request), while big maps comfortably fit them all.
constexpr int kMaxEnemyRequest = 30;

} // namespace

// =============================================================================
// Property 2: spawn placement is valid and distinct (R9.4, R9.6).
// =============================================================================
TEST_CASE("Property 2: spawn placement is valid and distinct") {
    // One master generator drives all randomized inputs, so the sequence of test
    // cases is fully determined by kMasterSeed (reproducible across runs).
    Rng master(kMasterSeed);

    // MapGenerator is a stateless service; a single instance builds every map.
    const MapGenerator generator;

    for (int iteration = 0; iteration < kIterations; ++iteration) {
        // ---------------------------------------------------------------------
        // Generators: draw a random-but-constrained input from the valid space.
        // Dimensions stay in [kMinDimension, kMaxDimension] so a Floor always
        // exists; the enemy request spans 0..kMaxEnemyRequest so both the
        // "fits everyone" and the "must reduce" (R9.6) cases are covered.
        // ---------------------------------------------------------------------
        const int width = master.rangeInt(kMinDimension, kMaxDimension);
        const int height = master.rangeInt(kMinDimension, kMaxDimension);
        const int requestedEnemies = master.rangeInt(0, kMaxEnemyRequest);

        // A per-case seed lets us rebuild this exact map/plan a second time for
        // the determinism check below, independently of the master sequence.
        const unsigned int caseSeed =
            static_cast<unsigned int>(master.rangeInt(0, 1000000000));

        // ---------------------------------------------------------------------
        // System under test: generate a play-ready map, then pick spawns on it.
        // generate() and pickSpawns() share one Rng so randomness flows in a
        // fixed, reproducible order for this case seed.
        // ---------------------------------------------------------------------
        Rng caseRng(caseSeed);
        GridMap map(width, height);
        generator.generate(map, caseRng, requestedEnemies);
        const SpawnPlan plan = generator.pickSpawns(map, caseRng, requestedEnemies);

        // Context attached to every failing assertion in this iteration.
        CAPTURE(iteration);
        CAPTURE(width);
        CAPTURE(height);
        CAPTURE(requestedEnemies);
        CAPTURE(caseSeed);

        // The Floor tiles actually available on the generated map. generate()
        // guarantees at least one Floor for grids this size, so availableFloors
        // is the real ceiling on how many distinct actors can be placed (R9.6).
        const std::vector<Vec2> floors = map.floorTiles();
        const int availableFloors = static_cast<int>(floors.size());
        CAPTURE(availableFloors);
        REQUIRE(availableFloors >= 1); // sanity: our generators never yield 0 floors.

        // ---------------------------------------------------------------------
        // Assertion 1 (R9.4): the Player starts on a valid, in-bounds Floor tile.
        // isWalkable() already folds in the in-bounds check and the Floor check,
        // and typeAt() confirms the tile kind explicitly for a clearer failure.
        // ---------------------------------------------------------------------
        INFO("playerStart=(", plan.playerStart.x, ",", plan.playerStart.y, ")");
        const bool playerInBounds = map.inBounds(plan.playerStart);
        CHECK(playerInBounds);
        const bool playerOnFloor = (map.typeAt(plan.playerStart) == TileType::Floor);
        CHECK(playerOnFloor);
        const bool playerWalkable = map.isWalkable(plan.playerStart);
        CHECK(playerWalkable);

        // ---------------------------------------------------------------------
        // Assertion 2 (R9.4): every enemy spawn is a valid, in-bounds Floor tile.
        // ---------------------------------------------------------------------
        for (std::size_t i = 0; i < plan.enemySpawns.size(); ++i) {
            const Vec2 spawn = plan.enemySpawns[i];
            INFO("enemySpawns[", i, "]=(", spawn.x, ",", spawn.y, ")");

            const bool spawnInBounds = map.inBounds(spawn);
            CHECK(spawnInBounds);
            const bool spawnOnFloor = (map.typeAt(spawn) == TileType::Floor);
            CHECK(spawnOnFloor);
            const bool spawnWalkable = map.isWalkable(spawn);
            CHECK(spawnWalkable);
        }

        // ---------------------------------------------------------------------
        // Assertion 3 (R9.4): all coordinates are pairwise distinct.
        //   (a) the Player tile differs from every enemy spawn, and
        //   (b) no two enemy spawns are equal.
        // ---------------------------------------------------------------------
        for (std::size_t i = 0; i < plan.enemySpawns.size(); ++i) {
            INFO("player-vs-enemy index i=", i);
            const bool distinctFromPlayer = (plan.playerStart != plan.enemySpawns[i]);
            CHECK(distinctFromPlayer);

            for (std::size_t j = i + 1; j < plan.enemySpawns.size(); ++j) {
                INFO("enemy-vs-enemy indices i=", i, " j=", j);
                const bool distinctEnemies =
                    (plan.enemySpawns[i] != plan.enemySpawns[j]);
                CHECK(distinctEnemies);
            }
        }

        // ---------------------------------------------------------------------
        // Assertion 4 (R9.6): the enemy-spawn count is reduced to fit the map.
        // pickSpawns reserves one Floor for the Player, so the number of enemy
        // spawns must equal min(requestedEnemies, availableFloors - 1): it never
        // exceeds the request, and it never exceeds the Floors left after the
        // Player tile is taken.
        // ---------------------------------------------------------------------
        const int actualEnemies = static_cast<int>(plan.enemySpawns.size());
        const int expectedEnemies = std::min(requestedEnemies, availableFloors - 1);
        CAPTURE(actualEnemies);
        CAPTURE(expectedEnemies);
        CHECK(actualEnemies == expectedEnemies);
        CHECK(actualEnemies <= requestedEnemies); // never more than asked for.

        // ---------------------------------------------------------------------
        // Assertion 5 (optional determinism): the same seed reproduces the same
        // SpawnPlan. Rebuild the identical map/plan from a freshly seeded Rng and
        // confirm every coordinate matches, reinforcing that placement is purely
        // a function of the seed (no hidden global randomness).
        // ---------------------------------------------------------------------
        Rng caseRngRepeat(caseSeed);
        GridMap mapRepeat(width, height);
        generator.generate(mapRepeat, caseRngRepeat, requestedEnemies);
        const SpawnPlan planRepeat =
            generator.pickSpawns(mapRepeat, caseRngRepeat, requestedEnemies);

        const bool samePlayerStart = (plan.playerStart == planRepeat.playerStart);
        CHECK(samePlayerStart);
        REQUIRE(plan.enemySpawns.size() == planRepeat.enemySpawns.size());
        for (std::size_t i = 0; i < plan.enemySpawns.size(); ++i) {
            INFO("determinism mismatch at enemy index i=", i);
            const bool sameSpawn = (plan.enemySpawns[i] == planRepeat.enemySpawns[i]);
            CHECK(sameSpawn);
        }
    }
}
