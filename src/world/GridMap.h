// =============================================================================
// world/GridMap.h
//
// Purpose:
//   GridMap is the dungeon level: a fixed-size 2D arrangement of Tiles that the
//   MapGenerator carves, the Pathfinder walks, and the renderer draws. It is the
//   project's "arrays" showcase (R5.1): the tiles are stored in a contiguous,
//   array-backed Grid<Tile> indexed by (x, y) grid coordinates.
//
//   GridMap's second job is to be the safe front door to that storage. Every
//   public accessor is bounds-guarded so that no caller - the generator, the AI,
//   a renderer, or a stray off-grid coordinate - can ever read or write outside
//   the tile array (R5.2, R5.3). Queries (isWalkable, typeAt) answer with a safe
//   default for out-of-bounds coordinates instead of throwing, because asking
//   "what is over there?" about a cell beyond the wall is a normal, expected
//   thing for game logic to do (a projectile leaving the map, an AI probing a
//   neighbor at the edge), not an error.
//
// Why a .h/.cpp split:
//   GridMap owns real logic (bounds-guarded lookups, collecting floor tiles), so
//   its declarations live here and its definitions live in GridMap.cpp, matching
//   the project's multi-file rule for non-trivial classes (R2.1).
//
// Layer: world (depends on core/Grid.h, core/Vec2.h, core/Enums.h, world/Tile.h).
// =============================================================================
#pragma once

#include <vector> // std::vector - return type of floorTiles().

#include "core/Enums.h" // TileType
#include "core/Grid.h"  // Grid<T> contiguous 2D container (arrays showcase)
#include "core/Vec2.h"  // Vec2 grid coordinate
#include "world/Tile.h" // Tile

namespace dga {

/// The dungeon grid for a single wave: a width x height field of Tiles.
///
/// All tile data lives in a contiguous Grid<Tile> (R5.1). The class exposes only
/// bounds-guarded operations, so collaborators interact with the map through a
/// safe interface and never index the underlying array directly.
class GridMap {
public:
    /// Construct a map of the given size, filled entirely with Wall tiles.
    ///
    /// A fresh map starts as solid rock on purpose: the MapGenerator later carves
    /// Floor into it, and a solid border of Wall means nothing can move outside
    /// the playable area (R9.3). Building "all walls" here is the safe default and
    /// avoids a separate clear step.
    ///
    /// @param width  number of columns; must be >= 0.
    /// @param height number of rows; must be >= 0.
    /// Negative dimensions are rejected by the underlying Grid<Tile> (it throws
    /// std::out_of_range), so a GridMap can never hold a malformed array.
    GridMap(int width, int height);

    /// @return the map width in tiles (number of columns).
    int width() const;

    /// @return the map height in tiles (number of rows).
    int height() const;

    /// Test whether a coordinate lies inside the map.
    /// @param x column index to test.
    /// @param y row index to test.
    /// @return true when (x, y) is a real cell of this map, false otherwise.
    bool inBounds(int x, int y) const;

    /// Test whether a coordinate lies inside the map.
    /// @param position the (x, y) coordinate to test.
    /// @return true when the coordinate is a real cell of this map.
    bool inBounds(const Vec2& position) const;

    /// Report whether a cell can be moved through.
    /// @param position the (x, y) coordinate to query.
    /// @return true only when the coordinate is in bounds AND its tile is Floor;
    ///         false for Wall tiles and for any out-of-bounds coordinate.
    /// This is a query, so it never throws: an out-of-bounds cell is simply not
    /// walkable, which lets movement and AI code probe neighbours freely (R5.3).
    bool isWalkable(const Vec2& position) const;

    /// Look up the kind of a cell.
    /// @param position the (x, y) coordinate to query.
    /// @return the TileType at that coordinate, or TileType::Wall when the
    ///         coordinate is out of bounds. Treating "outside the map" as Wall
    ///         models the impassable rock surrounding the level and keeps callers
    ///         from having to special-case the edges (R5.3); it never throws.
    TileType typeAt(const Vec2& position) const;

    /// Change the kind of a cell, if the coordinate is valid.
    /// @param position the (x, y) coordinate to modify.
    /// @param newType  the TileType to store there.
    /// Out-of-bounds coordinates are ignored (no write happens and nothing is
    /// thrown), so the MapGenerator can carve without bounds-checking every step
    /// and a bad coordinate can never corrupt memory outside the array (R5.2).
    void setType(const Vec2& position, TileType newType);

    /// Collect the coordinates of every Floor tile in the map.
    /// @return a vector of the (x, y) coordinates whose tile is Floor, in
    ///         row-major order (top row left-to-right, then the next row).
    /// Used for spawn placement: the generator picks the Player and enemy start
    /// cells from this list of walkable tiles (R9.4).
    std::vector<Vec2> floorTiles() const;

private:
    Grid<Tile> tiles_; ///< Contiguous, array-backed tile storage (R5.1).
};

} // namespace dga
