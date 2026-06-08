// =============================================================================
// world/GridMap.cpp
//
// Purpose:
//   Definitions for the GridMap class declared in world/GridMap.h. Every method
//   here is written so the map's contiguous Grid<Tile> can only ever be touched
//   through a bounds check, which is what makes out-of-bounds memory access
//   impossible from the public interface (R5.2, R5.3).
//
// Layer: world (depends on core/Grid.h, core/Vec2.h, core/Enums.h, world/Tile.h).
// =============================================================================
#include "world/GridMap.h"

namespace dga {

// Build the backing Grid<Tile> at the requested size, filling every cell with a
// Wall tile. The Grid constructor copies the fill value into all width*height
// cells, so the whole map starts as solid rock ready for the MapGenerator to
// carve (R9.3). Negative sizes are rejected inside Grid (it throws).
GridMap::GridMap(int width, int height)
    : tiles_(width, height, Tile(TileType::Wall)) {}

int GridMap::width() const {
    return tiles_.width();
}

int GridMap::height() const {
    return tiles_.height();
}

// Bounds checks delegate straight to Grid, the single place that knows the
// map's extent. Delegating avoids duplicating the comparison logic (R8.6).
bool GridMap::inBounds(int x, int y) const {
    return tiles_.inBounds(x, y);
}

bool GridMap::inBounds(const Vec2& position) const {
    return tiles_.inBounds(position);
}

// A query, never an error: a coordinate outside the map is simply not walkable,
// so we guard with inBounds() first and only consult the tile when it is safe.
// Short-circuit evaluation guarantees tiles_.at() is reached only for in-bounds
// coordinates, so this can never throw or read out-of-bounds memory (R5.3).
bool GridMap::isWalkable(const Vec2& position) const {
    return inBounds(position) && tiles_.at(position).isWalkable();
}

// Bounds-guarded read. Cells beyond the map are reported as Wall, modelling the
// impassable rock around the level so callers (projectiles, AI probing the edge)
// can treat the border uniformly without a special case (R5.3).
TileType GridMap::typeAt(const Vec2& position) const {
    if (!inBounds(position)) {
        return TileType::Wall;
    }
    return tiles_.at(position).type();
}

// Bounds-guarded write. An out-of-bounds coordinate is silently ignored so a bad
// coordinate can never write outside the tile array (R5.2); in-bounds writes go
// through Grid::at, which is itself checked.
void GridMap::setType(const Vec2& position, TileType newType) {
    if (!inBounds(position)) {
        return;
    }
    tiles_.at(position).setType(newType);
}

// Walk the whole grid in row-major order and collect the coordinate of every
// Floor cell. Reserving nothing special here keeps it simple; the map is small
// (tens of cells per side) so a single linear scan is plenty fast for spawn
// placement (R9.4).
std::vector<Vec2> GridMap::floorTiles() const {
    std::vector<Vec2> floors;
    for (int y = 0; y < tiles_.height(); ++y) {
        for (int x = 0; x < tiles_.width(); ++x) {
            const Vec2 cell(x, y);
            if (tiles_.at(cell).isWalkable()) {
                floors.push_back(cell);
            }
        }
    }
    return floors;
}

} // namespace dga
