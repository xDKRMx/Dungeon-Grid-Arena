// =============================================================================
// world/Tile.h
//
// Purpose:
//   Tile is the smallest unit of the dungeon: a single cell of the Grid_Map.
//   Each Tile remembers only one thing - its TileType (Floor or Wall) - and can
//   answer the one question the rest of the game asks about a cell in isolation:
//   "can something stand on / move through you?" (isWalkable, R9.2).
//
//   Occupancy (which entity is standing here) is deliberately NOT stored on the
//   Tile. The design keeps that state on the entities themselves so it is never
//   duplicated, which keeps the Tile tiny and the coupling low. The TileType is
//   wrapped in a small class (rather than using a bare TileType everywhere) so
//   there is room to grow later - for example a future "is this lit?" flag -
//   without changing every call site.
//
// Why a .h/.cpp split:
//   Although Tile is small, the task asks for a Tile.h/.cpp pair and the type
//   carries real (if simple) behavior, so its function bodies live in Tile.cpp
//   and only the declarations live here, matching the project's multi-file rule.
//
// Layer: world (depends only on core/Enums.h for TileType).
// =============================================================================
#pragma once

#include "core/Enums.h" // TileType { Floor, Wall }

namespace dga {

/// A single dungeon cell, identified by its TileType.
///
/// A Tile is a small value type: it is cheap to copy and is stored by value in
/// the GridMap's contiguous Grid<Tile>. Its single data member is kept private
/// and reached through member functions so the type can gain new fields later
/// without breaking callers (R1.2).
class Tile {
public:
    /// Construct a Tile of a given type.
    /// @param tileType the kind of cell this is (defaults to Wall).
    /// Defaulting to Wall means a freshly value-initialized Tile is impassable,
    /// which is the safe default: a brand-new GridMap is solid rock that the
    /// MapGenerator later carves Floor into, and nothing can accidentally walk
    /// through an uninitialized cell.
    explicit Tile(TileType tileType = TileType::Wall);

    /// @return the TileType stored in this cell.
    TileType type() const;

    /// Change what kind of cell this is.
    /// @param newType the TileType to store (Floor or Wall).
    void setType(TileType newType);

    /// Report whether an entity or projectile may occupy / pass over this cell.
    /// @return true when the cell is Floor; false when it is Wall (R9.2).
    bool isWalkable() const;

private:
    TileType type_; ///< The kind of this cell; the Tile's only state for now.
};

} // namespace dga
