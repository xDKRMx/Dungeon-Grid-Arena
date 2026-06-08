// =============================================================================
// world/MapGenerator.cpp
//
// Purpose:
//   Definitions for the MapGenerator class declared in world/MapGenerator.h.
//   This file is where the project's "connectivity guarantee" (R9) is actually
//   enforced: by the time generate() returns, the GridMap it carved is always
//   safe to play on.
//
//   Four cooperating pieces live here:
//     * generate()           - carve a random dungeon, then keep fixing it until
//                              it satisfies every play-readiness rule (R9.1-R9.6).
//     * isFullyConnected()   - a flood fill that PROVES every Floor tile can be
//                              reached from every other Floor tile (R9.2).
//     * repairConnectivity() - reconnect any stranded Floor pockets by carving
//                              straight corridors back to the largest region.
//     * pickSpawns()         - choose distinct Floor tiles for the Player and the
//                              enemies (R9.4), reducing the count if needed (R9.6).
//
// What a "flood fill" is (read once, used twice below):
//   Picture pouring water onto one Floor cell. The water spreads to any Floor
//   cell directly Up/Down/Left/Right of a wet cell, then keeps spreading from
//   those, and so on, until it can spread no further. Whatever ends up wet is
//   exactly the set of Floor tiles reachable from the start by orthogonal
//   movement. We implement that spread with a breadth-first search (a FIFO queue
//   of "cells to spread from"), the same technique the Pathfinder uses.
//
// Determinism:
//   Every random choice goes through the injected Rng, and every loop visits
//   cells in a fixed (row-major) order, so the same seed always produces the
//   exact same dungeon. That is what makes maps reproducible for tests and
//   faithful for save/load.
//
// Layer: world (depends on core/Vec2.h, core/Direction.h, core/Grid.h,
//        core/Enums.h, core/Rng.h, world/GridMap.h).
// =============================================================================
#include "world/MapGenerator.h"

#include <cstddef> // std::size_t - indexes the list of Floor groups.
#include <queue>   // std::queue  - the FIFO frontier that drives each flood fill.
#include <vector>  // std::vector - holds Floor groups and the spawn selection.

#include "core/Direction.h" // allDirections / toOffset - 4-directional neighbours.
#include "core/Enums.h"     // TileType (Floor / Wall).
#include "core/Grid.h"      // Grid<int> - the flood fill's visited buffer (R7.2).
#include "core/Rng.h"       // Rng - deterministic randomness source.
#include "world/GridMap.h"  // GridMap - isWalkable / typeAt / setType / floorTiles.

namespace dga {
namespace {

// -----------------------------------------------------------------------------
// Named constants (no magic numbers, per R8.5).
// -----------------------------------------------------------------------------

/// Visited-buffer markers used by the flood fill. A Grid<int> starts filled with
/// kUnvisited (int() == 0); a cell is flipped to kVisited the moment it is first
/// reached, so it is never enqueued twice.
constexpr int kUnvisited = 0;
constexpr int kVisited = 1;

/// Inclusive bounds of the percentage "dice roll" used when carving the interior.
/// rng.rangeInt(kPercentRollMin, kPercentRollMax) yields a value in [1, 100]; a
/// cell becomes Floor when that roll is <= kInteriorFloorPercent.
constexpr int kPercentRollMin = 1;
constexpr int kPercentRollMax = 100;

/// Roughly what fraction of interior cells start as Floor (the rest start as
/// Wall). A clear majority of Floor keeps the random map mostly open, which means
/// repairConnectivity has only a few small pockets to stitch back together.
constexpr int kInteriorFloorPercent = 60;

/// How many randomized carve attempts generate() makes before falling back to a
/// guaranteed-valid solid interior. The fallback (carveSolidInterior) always
/// succeeds, so this cap only bounds how long we spend chasing a "prettier"
/// random layout; it is not required for correctness.
constexpr int kMaxGenerationAttempts = 64;

/// The Player always needs exactly one start tile, in addition to the enemy
/// spawns. Naming it keeps the "+ 1" in the floor-count maths self-documenting.
constexpr int kPlayerTileCount = 1;

/// Test whether a coordinate lies on the outermost ring of the grid.
/// The border is always solid Wall so nothing can ever step off the map (R9.3),
/// so carving routines use this to leave the ring untouched.
/// @param x      column index of the cell.
/// @param y      row index of the cell.
/// @param width  grid width in tiles.
/// @param height grid height in tiles.
/// @return true when (x, y) is on the top, bottom, left, or right edge.
bool isBorderCell(int x, int y, int width, int height) {
    return x == 0 || y == 0 || x == width - 1 || y == height - 1;
}

/// Return the one-cell step (-1, 0, or +1) that moves `from` toward `to`.
/// Used to walk a corridor one tile at a time without assuming a direction.
/// @param from the current coordinate on one axis.
/// @param to   the target coordinate on the same axis.
/// @return +1 when `to` is larger, -1 when smaller, 0 when already equal.
int stepToward(int from, int to) {
    if (to > from) {
        return 1;
    }
    if (to < from) {
        return -1;
    }
    return 0;
}

/// Flood fill the connected Floor region that contains `start`.
///
/// Starting from `start`, this spreads to every orthogonally adjacent Floor tile
/// (Up/Down/Left/Right, OD-2) using a FIFO queue, marking each reached cell in
/// `visited` so it is counted exactly once. The cells it returns are precisely
/// the Floor tiles reachable from `start` - i.e. one connected region.
///
/// @param map     the map whose Floor tiles are being explored (read-only).
/// @param start   a Floor tile to begin the flood from; must be walkable.
/// @param visited a Grid<int> the size of the map, used to remember which cells
///                have already been reached. Cells reached here are set to
///                kVisited so a later flood does not revisit this region.
/// @return every Floor coordinate in the connected region containing `start`.
std::vector<Vec2> floodFillRegion(const GridMap& map, const Vec2& start,
                                   Grid<int>& visited) {
    std::vector<Vec2> region;
    std::queue<Vec2> frontier;

    // Seed the flood with the starting cell, marking it so it is never re-added.
    visited.at(start) = kVisited;
    frontier.push(start);

    while (!frontier.empty()) {
        const Vec2 current = frontier.front();
        frontier.pop();
        region.push_back(current);

        // Try to spread the flood into each orthogonal neighbour.
        for (const Direction direction : allDirections()) {
            const Vec2 neighbour = current + toOffset(direction);

            // isWalkable() already folds in the in-bounds check, so whenever it
            // is true the neighbour is a real Floor cell of the map; because the
            // visited buffer has the same dimensions as the map, visited.at() is
            // then guaranteed to be in range. We only spread to a Floor cell that
            // has not been reached yet.
            if (map.isWalkable(neighbour) && visited.at(neighbour) == kUnvisited) {
                visited.at(neighbour) = kVisited;
                frontier.push(neighbour);
            }
        }
    }

    return region;
}

/// Fill the whole grid with a random mix of Floor and Wall in the interior, with
/// a solid Wall border (R9.1, R9.3).
///
/// Every interior cell independently "rolls the dice": it becomes Floor when a
/// uniform roll in [1, 100] is at most `floorPercent`, otherwise Wall. Border
/// cells are always Wall. Cells are visited in a fixed row-major order and the
/// border cells consume no randomness, so the Rng is driven identically for a
/// given seed (determinism).
///
/// @param map         the grid to overwrite in place.
/// @param rng         deterministic randomness source for the dice rolls.
/// @param floorPercent the approximate percentage of interior cells made Floor.
void carveRandomInterior(GridMap& map, Rng& rng, int floorPercent) {
    const int width = map.width();
    const int height = map.height();

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const Vec2 cell(x, y);

            if (isBorderCell(x, y, width, height)) {
                map.setType(cell, TileType::Wall); // Solid border (R9.3).
                continue;
            }

            const int roll = rng.rangeInt(kPercentRollMin, kPercentRollMax);
            const bool makeFloor = roll <= floorPercent;
            map.setType(cell, makeFloor ? TileType::Floor : TileType::Wall);
        }
    }
}

/// Fill the entire interior with Floor, keeping a solid Wall border.
///
/// This is generate()'s guaranteed-valid fallback: a solid rectangle of Floor is
/// always a single connected region and contains the largest possible number of
/// Floor tiles, so it satisfies both the connectivity (R9.2) and "enough floors"
/// (R9.5) rules no matter how small or awkward the grid is. It uses no
/// randomness, so it is trivially deterministic.
///
/// @param map the grid to overwrite in place.
void carveSolidInterior(GridMap& map) {
    const int width = map.width();
    const int height = map.height();

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const Vec2 cell(x, y);
            const bool border = isBorderCell(x, y, width, height);
            map.setType(cell, border ? TileType::Wall : TileType::Floor);
        }
    }
}

/// Carve a straight, L-shaped corridor of Floor from `from` to `to`.
///
/// The corridor has two legs: first a horizontal run along row `from.y` from
/// column `from.x` to column `to.x`, then a vertical run along column `to.x`
/// from row `from.y` to row `to.y`. Every cell on that path is set to Floor, so
/// the carve only ever turns Wall into Floor (existing Floor stays Floor) - it
/// can never create a hole in the border, because both endpoints are interior
/// Floor tiles and the L therefore stays inside the interior bounding box.
///
/// @param map  the grid to carve into in place.
/// @param from one endpoint of the corridor (an interior Floor tile).
/// @param to   the other endpoint of the corridor (an interior Floor tile).
void carveCorridor(GridMap& map, const Vec2& from, const Vec2& to) {
    // Horizontal leg: walk the columns along row `from.y` until we reach to.x.
    const int stepX = stepToward(from.x, to.x);
    int x = from.x;
    while (true) {
        map.setType(Vec2(x, from.y), TileType::Floor);
        if (x == to.x) {
            break;
        }
        x += stepX;
    }

    // Vertical leg: walk the rows along column `to.x` until we reach to.y. The
    // corner cell (to.x, from.y) is shared with the horizontal leg, which is
    // harmless because re-carving an already-Floor cell changes nothing.
    const int stepY = stepToward(from.y, to.y);
    int y = from.y;
    while (true) {
        map.setType(Vec2(to.x, y), TileType::Floor);
        if (y == to.y) {
            break;
        }
        y += stepY;
    }
}

/// Find the index of the largest Floor region in a list of regions.
/// repairConnectivity keeps this region as the "mainland" and connects every
/// other region to it, which minimises the number of corridors that have to be
/// carved.
/// @param regions the connected Floor regions discovered by flood fill.
/// @return the index of the region holding the most cells (the first such region
///         on a tie, since the scan keeps the earliest maximum).
std::size_t indexOfLargestRegion(const std::vector<std::vector<Vec2>>& regions) {
    std::size_t largest = 0;
    for (std::size_t index = 1; index < regions.size(); ++index) {
        if (regions[index].size() > regions[largest].size()) {
            largest = index;
        }
    }
    return largest;
}

} // namespace

// -----------------------------------------------------------------------------
// generate: carve a random dungeon, then repair/retry until it is play-ready.
// -----------------------------------------------------------------------------
void MapGenerator::generate(GridMap& map, Rng& rng, int enemySpawnCount) const {
    // Treat a negative request as zero spawns (defensive, per the header note).
    const int requestedSpawns = (enemySpawnCount > 0) ? enemySpawnCount : 0;

    // How many Floor tiles play requires: one for the Player plus one per enemy
    // spawn, so each actor can stand on its own distinct tile (R9.5).
    const int requiredFloors = kPlayerTileCount + requestedSpawns;

    // The interior is the carve-able area inside the solid border. A grid that is
    // 2 or fewer cells on a side has no interior at all, so clamp to zero rather
    // than computing a negative size.
    const int interiorWidth = (map.width() > 2) ? map.width() - 2 : 0;
    const int interiorHeight = (map.height() > 2) ? map.height() - 2 : 0;
    const int interiorCapacity = interiorWidth * interiorHeight;

    // We can never carve more Floor than the interior physically holds, so the
    // achievable target is the smaller of "what play wants" and "what fits". When
    // the grid is too small to seat everyone, this is how R9.6 is honoured up
    // front: we aim for the maximum possible instead of an impossible number.
    const int feasibleFloors =
        (requiredFloors < interiorCapacity) ? requiredFloors : interiorCapacity;

    // Try a handful of random layouts. After each carve we repair connectivity,
    // then accept the map only once it is BOTH fully connected (R9.2) AND holds
    // enough Floor tiles (R9.5). repairConnectivity already guarantees the first
    // condition, so in practice this loop succeeds on the first attempt unless an
    // unlucky carve produced too few floors on a small grid.
    for (int attempt = 0; attempt < kMaxGenerationAttempts; ++attempt) {
        carveRandomInterior(map, rng, kInteriorFloorPercent);
        repairConnectivity(map, rng);

        const int floorCount = static_cast<int>(map.floorTiles().size());
        if (isFullyConnected(map) && floorCount >= feasibleFloors) {
            return;
        }
    }

    // Fallback: a solid interior is always connected and maximises the floor
    // count, so play never begins on an invalid map (R9.5). This makes the whole
    // routine total - it cannot fail to produce a play-ready map.
    carveSolidInterior(map);
}

// -----------------------------------------------------------------------------
// isFullyConnected: flood fill from the first Floor; connected iff it reaches all.
// -----------------------------------------------------------------------------
bool MapGenerator::isFullyConnected(const GridMap& map) const {
    const std::vector<Vec2> floors = map.floorTiles();

    // A map with zero or one Floor tile cannot contain a "disconnected" pocket,
    // so it is connected by definition - there is nothing to flood to.
    if (floors.size() <= 1) {
        return true;
    }

    // Flood the single region that contains the first Floor tile. If that flood
    // reaches every Floor tile the map has, all floors form one region; if it
    // reaches fewer, some floors are stranded in a separate pocket (R9.2).
    Grid<int> visited(map.width(), map.height(), kUnvisited);
    const std::vector<Vec2> reached = floodFillRegion(map, floors.front(), visited);
    return reached.size() == floors.size();
}

// -----------------------------------------------------------------------------
// repairConnectivity: join every stranded Floor pocket to the largest region.
// -----------------------------------------------------------------------------
void MapGenerator::repairConnectivity(GridMap& map, Rng& rng) const {
    // First, discover every connected Floor region. We scan the floor tiles in
    // row-major order and, whenever we meet a tile we have not flooded yet, flood
    // its whole region and record it. The shared `visited` buffer ensures each
    // tile is assigned to exactly one region.
    Grid<int> visited(map.width(), map.height(), kUnvisited);
    std::vector<std::vector<Vec2>> regions;

    for (const Vec2& floor : map.floorTiles()) {
        if (visited.at(floor) == kUnvisited) {
            regions.push_back(floodFillRegion(map, floor, visited));
        }
    }

    // Zero regions (no floors) or a single region means there is nothing to
    // reconnect - the map is already connected, so this is a no-op.
    if (regions.size() <= 1) {
        return;
    }

    // Keep the biggest region as the "mainland" and connect every other region to
    // it. Once each pocket has a corridor to the mainland, all floors share one
    // region, so the whole map becomes connected.
    const std::size_t mainlandIndex = indexOfLargestRegion(regions);
    const std::vector<Vec2>& mainland = regions[mainlandIndex];

    for (std::size_t index = 0; index < regions.size(); ++index) {
        if (index == mainlandIndex) {
            continue; // The mainland does not need connecting to itself.
        }

        // Pick one cell from the pocket and one cell from the mainland to join.
        // Both picks go through the Rng, so a given seed always repairs a given
        // layout the same way (determinism). Carving Wall->Floor only, the L
        // corridor stays inside the interior and never breaches the border.
        const Vec2& pocketCell = rng.choice(regions[index]);
        const Vec2& mainlandCell = rng.choice(mainland);
        carveCorridor(map, pocketCell, mainlandCell);
    }
}

// -----------------------------------------------------------------------------
// pickSpawns: choose distinct Floor tiles for the Player and the enemies.
// -----------------------------------------------------------------------------
SpawnPlan MapGenerator::pickSpawns(const GridMap& map, Rng& rng,
                                   int enemySpawnCount) const {
    SpawnPlan plan;

    // Every candidate start tile is a Floor tile; if the map somehow has none,
    // there is nowhere to place anyone, so return an empty plan.
    std::vector<Vec2> floors = map.floorTiles();
    if (floors.empty()) {
        return plan;
    }

    // Treat a negative request as zero, then work out how many distinct tiles we
    // can actually hand out: the Player tile plus the requested enemy tiles,
    // capped at the number of Floor tiles available. Capping here is exactly how
    // R9.6 is honoured - if the map cannot fit every spawn, we quietly return
    // fewer enemy spawns rather than reusing a tile.
    const int requestedSpawns = (enemySpawnCount > 0) ? enemySpawnCount : 0;
    const int wanted = kPlayerTileCount + requestedSpawns;
    const int floorCount = static_cast<int>(floors.size());
    const int selectCount = (wanted < floorCount) ? wanted : floorCount;

    // Select `selectCount` DISTINCT tiles with a partial Fisher-Yates shuffle:
    // for each slot we swap in a tile chosen uniformly from the not-yet-chosen
    // remainder of the list. Because every pick comes from the untouched tail,
    // no tile can be chosen twice, which guarantees the Player tile and all enemy
    // tiles are pairwise distinct (R9.4). All randomness is via the Rng, so the
    // placement is reproducible for a given seed.
    for (int slot = 0; slot < selectCount; ++slot) {
        const int lastIndex = floorCount - 1;
        const int pick = rng.rangeInt(slot, lastIndex);

        const Vec2 chosen = floors[static_cast<std::size_t>(pick)];
        floors[static_cast<std::size_t>(pick)] = floors[static_cast<std::size_t>(slot)];
        floors[static_cast<std::size_t>(slot)] = chosen;
    }

    // The first chosen tile seats the Player; the rest seat the enemies. Because
    // selectCount is at least 1 whenever any Floor exists, the Player always gets
    // a tile, and enemySpawns holds however many tiles were left over (R9.4, R9.6).
    plan.playerStart = floors.front();
    plan.enemySpawns.assign(floors.begin() + kPlayerTileCount,
                            floors.begin() + selectCount);
    return plan;
}

} // namespace dga
