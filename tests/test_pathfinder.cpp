// Feature: dungeon-grid-arena, Property 4: BFS path correctness and minimality
// =============================================================================
// tests/test_pathfinder.cpp
//
// Property-based test for dga::Pathfinder (world/Pathfinder.{h,cpp}).
//
// Property 4 (from design.md "Correctness Properties"):
//   "For any map and any from/to floor tiles, Pathfinder.findPath returns an
//    empty path if and only if no orthogonal walkable path exists; otherwise the
//    returned path starts at `from`, ends at `to`, every consecutive pair is
//    orthogonally adjacent, every tile on it is walkable (never a Wall or
//    occupied tile), and its length equals the true shortest distance."
//   Validates: Requirements 12.1, 12.4, 12.5.
//
// How this file tests that property:
//   We drive everything from a single SEEDED dga::Rng, so the entire test is
//   deterministic and reproducible: the same seed always builds the same maps,
//   start/goal tiles, and blocked sets. On each of >= 100 randomized iterations
//   we
//     1. generate a guaranteed-connected dungeon with dga::MapGenerator,
//     2. pick random Floor tiles for `start` and `goal` (sometimes equal),
//     3. optionally mark a random subset of Floor tiles as "blocked"
//        (entities standing in the way), and
//     4. compare dga::Pathfinder::findPath against an INDEPENDENT breadth-first
//        search written from scratch in this file (never calling Pathfinder),
//        which serves as the ground-truth shortest distance / reachability.
//
//   Crucial contract detail (read from Pathfinder.h / Pathfinder.cpp): the GOAL
//   tile is EXEMPT from the blocked check, because it is the search target (the
//   player's tile). A neighbour is a valid BFS expansion when it is walkable AND
//   (it is the goal OR it is not blocked). Our independent BFS replicates that
//   exact rule so the two agree on both reachability and distance.
//
// Note: this TU includes "doctest.h" WITHOUT the implementation macro;
// tests/test_main.cpp already provides doctest's main().
// =============================================================================
#include "doctest.h"

#include <queue>  // std::queue - frontier for the independent ground-truth BFS
#include <vector> // std::vector - paths, blocked lists, distance buffer

#include "core/Rng.h"            // dga::Rng - the one deterministic randomness source
#include "core/Vec2.h"           // dga::Vec2 - grid coordinate
#include "world/GridMap.h"       // dga::GridMap - isWalkable / inBounds / width / height
#include "world/MapGenerator.h"  // dga::MapGenerator - connected dungeon generator
#include "world/Pathfinder.h"    // dga::Pathfinder - the subject under test

namespace {

using dga::GridMap;
using dga::MapGenerator;
using dga::Pathfinder;
using dga::Rng;
using dga::Vec2;

// The four orthogonal moves, hardcoded on purpose: keeping them local (instead
// of reusing core/Direction.h) makes this ground-truth BFS visibly independent
// of any production helper the Pathfinder might share.
constexpr int kStepCount = 4;
const Vec2 kSteps[kStepCount] = {
    Vec2(0, -1), // up
    Vec2(0, 1),  // down
    Vec2(-1, 0), // left
    Vec2(1, 0),  // right
};

// Sentinel meaning "this cell has not been reached yet" in the BFS buffer.
constexpr int kUnreached = -1;

/// Linear membership test mirroring Pathfinder's own isBlocked helper.
/// @return true when `tile` appears in `blocked`.
bool tileIsBlocked(const Vec2& tile, const std::vector<Vec2>& blocked) {
    for (const Vec2& occupied : blocked) {
        if (occupied == tile) {
            return true;
        }
    }
    return false;
}

/// Independent, from-scratch BFS that returns the TRUE shortest distance (in
/// steps) from `start` to `goal`, or kUnreached (-1) when no path exists.
///
/// This is the ground truth we hold Pathfinder to. It deliberately re-derives
/// reachability with the SAME rules Pathfinder documents, so the two must agree:
///   - both endpoints must be walkable Floor tiles, else there is no path;
///   - start == goal is distance 0;
///   - a neighbour is traversable when it is walkable AND (it is the goal OR it
///     is not in `blocked`) - the goal is exempt from the blocked check.
/// It never calls Pathfinder, so it cannot mask a bug by sharing its logic.
int independentShortestDistance(const GridMap& map, const Vec2& start,
                                const Vec2& goal,
                                const std::vector<Vec2>& blocked) {
    // Endpoints off the map or on a Wall mean there is nothing to search (R12.4).
    if (!map.isWalkable(start) || !map.isWalkable(goal)) {
        return kUnreached;
    }
    // Standing on the goal already: zero steps away.
    if (start == goal) {
        return 0;
    }

    // Row-major distance buffer sized to the map; every cell starts unreached.
    std::vector<int> distance(static_cast<std::size_t>(map.width()) *
                                  static_cast<std::size_t>(map.height()),
                              kUnreached);
    const auto indexOf = [&map](const Vec2& cell) {
        return static_cast<std::size_t>(cell.y) *
                   static_cast<std::size_t>(map.width()) +
               static_cast<std::size_t>(cell.x);
    };

    std::queue<Vec2> frontier;
    distance[indexOf(start)] = 0;
    frontier.push(start);

    while (!frontier.empty()) {
        const Vec2 current = frontier.front();
        frontier.pop();
        if (current == goal) {
            break; // BFS has finalized the goal's distance.
        }
        const int nextDistance = distance[indexOf(current)] + 1;

        for (const Vec2& step : kSteps) {
            const Vec2 neighbour = current + step;
            if (!map.inBounds(neighbour)) {
                continue; // Guard the buffer index against off-grid cells.
            }
            const bool walkable = map.isWalkable(neighbour);
            const bool free =
                (neighbour == goal) || !tileIsBlocked(neighbour, blocked);
            const bool unseen = distance[indexOf(neighbour)] == kUnreached;
            if (walkable && free && unseen) {
                distance[indexOf(neighbour)] = nextDistance;
                frontier.push(neighbour);
            }
        }
    }

    return distance[indexOf(goal)];
}

} // namespace

// -----------------------------------------------------------------------------
// Property 4: BFS path correctness and minimality (R12.1, R12.4, R12.5).
// -----------------------------------------------------------------------------
TEST_CASE("Property 4: Pathfinder.findPath is correct, valid, and minimal") {
    // A single fixed seed makes the whole test reproducible: every random choice
    // below (map size, carve, start/goal, blocked set) flows from this one Rng.
    constexpr unsigned int kSeed = 0xC0FFEEu;
    Rng rng(kSeed);

    const MapGenerator generator;
    const Pathfinder pathfinder;

    // >= 100 randomized iterations, per the project's PBT setup (design.md).
    constexpr int kIterations = 200;

    for (int iteration = 0; iteration < kIterations; ++iteration) {
        // --- Generate a random, guaranteed-connected dungeon. ---
        // Dimensions stay small but always leave an interior (width/height > 2),
        // which keeps each iteration fast while still varying the map shape.
        const int width = rng.rangeInt(5, 14);
        const int height = rng.rangeInt(5, 14);
        GridMap map(width, height);
        // The spawn count only sizes the "enough floors" guarantee; we just need
        // the carved Floor layout, so a small number is plenty.
        generator.generate(map, rng, /*enemySpawnCount=*/3);

        const std::vector<Vec2> floors = map.floorTiles();
        // generate() guarantees at least one Floor tile, but assert it so a
        // regression there surfaces here instead of as a confusing index error.
        REQUIRE_FALSE(floors.empty());
        const int floorCount = static_cast<int>(floors.size());

        // --- Pick random start and goal Floor tiles (they may coincide). ---
        const Vec2 start = floors[static_cast<std::size_t>(
            rng.rangeInt(0, floorCount - 1))];
        const Vec2 goal = floors[static_cast<std::size_t>(
            rng.rangeInt(0, floorCount - 1))];

        // --- Optionally block a random subset of Floor tiles. ---
        // Each Floor tile other than `start` is blocked with ~25% probability.
        // We never block `start` so that path[0] is always an unblocked tile and
        // the "no non-goal tile is occupied" assertion stays unambiguous; `goal`
        // MAY be blocked, which intentionally exercises the goal-is-exempt rule.
        std::vector<Vec2> blocked;
        for (const Vec2& floor : floors) {
            if (floor != start && rng.rangeInt(1, 100) <= 25) {
                blocked.push_back(floor);
            }
        }

        // --- Run the subject and the independent ground truth. ---
        const std::vector<Vec2> path =
            pathfinder.findPath(map, start, goal, blocked);
        const int groundTruthDistance =
            independentShortestDistance(map, start, goal, blocked);

        // Make any failure self-describing (doctest prints these on a CHECK fail).
        INFO("iteration=" << iteration << " seed=" << kSeed
                          << " dims=" << width << "x" << height
                          << " start=(" << start.x << "," << start.y << ")"
                          << " goal=(" << goal.x << "," << goal.y << ")"
                          << " blockedCount=" << blocked.size()
                          << " pathLen=" << path.size()
                          << " gtDistance=" << groundTruthDistance);

        if (groundTruthDistance == kUnreached) {
            // R12.4: when the goal is genuinely unreachable (cross-checked by our
            // own BFS), findPath MUST return an empty path - the "only if" half of
            // the biconditional.
            CHECK(path.empty());
        } else {
            // The "if" half: a path exists, so findPath must return a non-empty,
            // valid, minimal route.
            REQUIRE_FALSE(path.empty());

            // (a) Endpoints: the path begins at `start` and ends at `goal`
            //     inclusive (R12.1 / Pathfinder.h contract).
            CHECK(path.front() == start);
            CHECK(path.back() == goal);

            // (b) Every step is a single orthogonal move: consecutive tiles are
            //     Manhattan distance 1 apart (no diagonals, no teleports) - the
            //     4-directional movement model (OD-2, R12.1).
            for (std::size_t i = 1; i < path.size(); ++i) {
                CHECK(path[i - 1].manhattan(path[i]) == 1);
            }

            // (c) Every tile on the path is a walkable Floor tile (never a Wall
            //     or off-map cell), and no NON-goal tile is occupied/blocked. The
            //     goal is exempt from the blocked check by design, so we only
            //     require it to be walkable (R12.4, R12.5).
            for (const Vec2& tile : path) {
                CHECK(map.isWalkable(tile));
                if (tile != goal) {
                    CHECK_FALSE(tileIsBlocked(tile, blocked));
                }
            }

            // (d) The path contains no repeated tiles. A shortest path never
            //     revisits a cell; this also rules out an accidental cycle in the
            //     reconstruction (R12.1).
            for (std::size_t i = 0; i < path.size(); ++i) {
                for (std::size_t j = i + 1; j < path.size(); ++j) {
                    CHECK(path[i] != path[j]);
                }
            }

            // (e) Minimality: the number of STEPS (tiles minus one) equals the
            //     true shortest distance computed by our independent BFS. This is
            //     the heart of the property - the path is not just valid, it is
            //     as short as possible (R12.1, R12.5).
            const int pathSteps = static_cast<int>(path.size()) - 1;
            CHECK(pathSteps == groundTruthDistance);
        }

        // --- nextStepToward agrees with findPath's first move (R12.4). ---
        // It returns `start` when there is no path or the mover is already at the
        // goal (path size < 2), otherwise the SECOND tile of the path.
        const Vec2 nextStep = pathfinder.nextStepToward(map, start, goal, blocked);
        if (path.size() < 2) {
            CHECK(nextStep == start);
        } else {
            CHECK(nextStep == path[1]);
        }
    }
}
