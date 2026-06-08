// =============================================================================
// world/Tile.cpp
//
// Purpose:
//   Definitions for the Tile class declared in world/Tile.h. The bodies are
//   intentionally tiny - a Tile is just a typed wrapper around a TileType - but
//   they live here (rather than inline in the header) so the type follows the
//   project's declaration-in-header / definition-in-source convention.
//
// Layer: world (depends only on core/Enums.h via the header).
// =============================================================================
#include "world/Tile.h"

namespace dga {

// Store the requested type. The default argument (Wall) is declared in the
// header, so a default-constructed Tile begins life as an impassable wall.
Tile::Tile(TileType tileType) : type_(tileType) {}

// Simple read-only accessor for the stored cell type.
TileType Tile::type() const {
    return type_;
}

// Overwrite the stored cell type (used by the MapGenerator while carving).
void Tile::setType(TileType newType) {
    type_ = newType;
}

// A cell is walkable only when it is Floor; every other type (currently just
// Wall) blocks movement (R9.2). Comparing against the named enumerator keeps
// this free of magic numbers and reads like the rule it implements.
bool Tile::isWalkable() const {
    return type_ == TileType::Floor;
}

} // namespace dga
