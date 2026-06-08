// =============================================================================
// world/MapGenerator.h
//
// Purpose:
//   MapGenerator turns an empty GridMap into a playable dungeon for one Wave. It
//   is the home of the project's "connectivity guarantee" (R9): the map it hands
//   back is always safe to play on, meaning
//
//     * the whole playable area is ringed by Wall tiles, so nothing can ever
//       step outside the grid (R9.3);
//     * every Floor tile can be reached from every other Floor tile by walking
//       orthogonally over Floor only - there are no sealed-off pockets (R9.2);
//     * there are at least enough Floor tiles to stand the Player plus all the
//       enemy spawns the caller asked for (R9.5, R9.6).
//
//   The class is a *stateless service*: it stores nothing of its own and simply
//   operates on the GridMap and Rng it is handed. Routing all randomness through
//   the injected Rng keeps generation deterministic - the same seed always
//   produces the same dungeon - which is what makes maps reproducible for tests
//   and faithful for save/load.
//
// How the connectivity guarantee is achieved (plain-language overview):
//   1. generate() carves a random mix of Floor and Wall into the interior and
//      walls off the border.
//   2. A "flood fill" (see isFullyConnected) checks whether every Floor is
//      reachable from one starting Floor. A flood fill is just: start on one
//      cell, repeatedly spread to its Floor neighbours, and see how far the
//      "flood" of water can spread. If it reaches every Floor, the map is fully
//      connected; if some Floors stay dry, they are stranded.
//   3. repairConnectivity() reconnects any stranded pockets by carving straight
//      corridors back to the main area.
//   4. generate() repeats until both the connectivity and "enough floors"
//      conditions hold, so play never begins on an invalid map (R9.5).
//
// Why a .h/.cpp split:
//   MapGenerator owns real algorithms (random carving, flood fill, corridor
//   repair, spawn selection), so its declarations live here and its definitions
//   live in MapGenerator.cpp, matching the project's multi-file rule for
//   non-trivial classes (R2.1).
//
// Layer: world (depends on core/Vec2.h, core/Rng.h, core/Direction.h, and
//        world/GridMap.h - all defined in MapGenerator.cpp).
// =============================================================================
#pragma once

#include <vector> // std::vector - holds the enemy spawn coordinates.

#include "core/Vec2.h" // Vec2 grid coordinate (stored by value in SpawnPlan).

namespace dga {

// Forward declarations keep this header light: the full definitions are only
// needed inside MapGenerator.cpp, so callers that merely hold a MapGenerator do
// not have to pull in GridMap or Rng.
class GridMap;
class Rng;

/// The result of choosing where actors begin the Wave (R9.4).
///
/// The Player start and the enemy spawns are returned as two clearly named
/// fields rather than a single bag of coordinates, so a caller never has to
/// remember a convention like "index 0 is the player". Every coordinate is a
/// valid Floor tile, and all of them (the Player tile and every enemy tile) are
/// pairwise distinct.
struct SpawnPlan {
    Vec2 playerStart;              ///< Floor tile the hero begins on (R9.4).
    std::vector<Vec2> enemySpawns; ///< Distinct Floor tiles for the enemies
                                   ///< (R9.4); may hold fewer than requested if
                                   ///< the map cannot fit them all (R9.6).
};

/// Procedural dungeon builder with a built-in connectivity guarantee (R9).
///
/// MapGenerator holds no data members of its own: it is a service that reads and
/// rewrites a GridMap using an injected Rng. Because it carries no state, every
/// member function is `const` and a single shared instance can build any number
/// of maps.
class MapGenerator {
public:
    /// Carve a complete, ready-to-play dungeon into `map`.
    ///
    /// On return the map is guaranteed to satisfy every play-readiness rule:
    /// the border is solid Wall (R9.3), all Floor tiles are mutually reachable
    /// (R9.2), and there are at least `1 + enemySpawnCount` Floor tiles so the
    /// Player and every requested enemy can stand on a distinct tile (R9.5).
    /// generate() keeps re-carving and repairing until those conditions hold, so
    /// it never leaves behind an invalid map (R9.5).
    ///
    /// @param map             the grid to carve into; its existing contents are
    ///                        overwritten. Its width/height set the dungeon size.
    /// @param rng             the deterministic randomness source; the same
    ///                        seeded Rng always yields the same dungeon.
    /// @param enemySpawnCount how many enemy spawn tiles the caller will need in
    ///                        addition to the Player tile. Used to size the
    ///                        "enough floors" guarantee (R9.6). Negative values
    ///                        are treated as zero.
    /// Note: if the configured grid is physically too small to hold the Player
    /// plus every requested spawn, generate() still produces the maximum number
    /// of connected Floor tiles it can; pickSpawns() then honours R9.6 by
    /// returning fewer enemy spawns.
    void generate(GridMap& map, Rng& rng, int enemySpawnCount) const;

    /// Report whether every Floor tile is reachable from every other Floor tile.
    ///
    /// Uses a flood fill (breadth-first search) starting from the first Floor
    /// tile and spreading through orthogonal Floor neighbours only (4-directional
    /// movement, OD-2). The map is fully connected exactly when the flood reaches
    /// the same number of Floor tiles as the map actually contains. A map with
    /// zero or one Floor tile is trivially connected.
    ///
    /// @param map the map to inspect (not modified).
    /// @return true when all Floor tiles form a single connected region (R9.2).
    bool isFullyConnected(const GridMap& map) const;

    /// Reconnect any Floor pockets that the random carve left stranded.
    ///
    /// Finds every connected group of Floor tiles, keeps the largest as the
    /// "mainland", and carves a straight L-shaped corridor from one cell of each
    /// other group back to the mainland. Because every group ends up joined to
    /// the mainland, the whole map becomes a single connected region. Does
    /// nothing when the map is already connected (or has no floors).
    ///
    /// @param map the map to repair in place; only ever turns Wall into Floor.
    /// @param rng randomness used to pick which cells to connect, kept
    ///            deterministic so a given seed always repairs the same way.
    void repairConnectivity(GridMap& map, Rng& rng) const;

    /// Choose distinct Floor tiles for the Player and the enemies (R9.4).
    ///
    /// All returned coordinates are valid Floor tiles and are pairwise distinct:
    /// the Player tile differs from every enemy tile, and no two enemy tiles
    /// repeat. If the map holds fewer Floor tiles than requested, the number of
    /// enemy spawns is reduced to what fits, satisfying R9.6.
    ///
    /// @param map             the (already generated) map to place actors on.
    /// @param rng             deterministic randomness for the placement.
    /// @param enemySpawnCount how many enemy spawn tiles to try to choose.
    ///                        Negative values are treated as zero.
    /// @return a SpawnPlan whose playerStart and enemySpawns are all distinct
    ///         Floor tiles. enemySpawns may be shorter than enemySpawnCount when
    ///         the map cannot fit them all (R9.6).
    SpawnPlan pickSpawns(const GridMap& map, Rng& rng, int enemySpawnCount) const;
};

} // namespace dga
