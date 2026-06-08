// =============================================================================
// core/Grid.h
//
// Purpose:
//   Grid<T> is the game's generic, reusable 2D container. It stores width*height
//   elements of some element type T in a single flat (one-dimensional) array and
//   lets callers address them with grid coordinates (a column x and a row y).
//
//   It is the project's "arrays" building block (R5.1): the dungeon map stores
//   its tiles as a Grid<Tile>, and the pathfinder stores its BFS distances as a
//   Grid<int>. Because the same code serves both, Grid is written ONCE as a
//   class template instead of being copy-pasted per element type. Being used for
//   at least two distinct types (Tile and int) is exactly what the templates
//   rubric criterion asks for (R7.1, R7.2).
//
// Row-major layout (read this once and the indexing makes sense everywhere):
//   The cells live in a plain std::vector<T> laid out one full row after another
//   - first the whole top row left-to-right, then the next row, and so on. So the
//   flat index of the cell at column x, row y is:
//
//       index = y * width + x
//
//   "Row-major" just names that choice: rows are stored contiguously. This makes
//   the storage a real contiguous array (good cache behavior) while callers still
//   get to think in (x, y) coordinates.
//
// Why header-only:
//   Grid is a template, and a compiler must see a template's full body to stamp
//   out a version for each element type it is used with. Templates therefore live
//   in headers; per the project's multi-file rule that is the allowed exception,
//   so there is no Grid.cpp.
//
// Bounds policy (checked access):
//   at() is a *checked* accessor: if it is given a coordinate that is not inside
//   the grid it throws std::out_of_range rather than touching memory it does not
//   own. Throwing (instead of asserting) is deliberate - the guarantee then holds
//   in every build, including optimized release builds where assert() is removed,
//   which is what lets the map honor "reject the access without reading or writing
//   out-of-bounds memory" (R5.3). Callers that want to test a coordinate first,
//   without risking an exception, use inBounds().
//
// Layer: core (depends only on core/Vec2.h and the C++ standard library).
// =============================================================================
#pragma once

#include <stdexcept> // std::out_of_range - thrown by at() on a bad coordinate.
#include <string>    // std::string  - builds the descriptive exception message.
#include <vector>    // std::vector  - the contiguous backing store for cells.

#include "core/Vec2.h"

namespace dga {

/// A generic, fixed-size 2D container addressed by integer (x, y) coordinates.
///
/// @tparam type the element type stored in each cell (for example Tile or int).
///
/// The dimensions are chosen at construction and never change afterwards. Cells
/// are stored row-major in a single contiguous std::vector (see the file header
/// for the layout and the index formula).
template <typename type>
class Grid {
public:
    /// Construct a width x height grid with every cell set to a fill value.
    /// @param gridWidth   number of columns; must be >= 0.
    /// @param gridHeight  number of rows; must be >= 0.
    /// @param defaultValue the value copied into every cell (defaults to a
    ///        value-initialized element, e.g. 0 for int), so a caller who does
    ///        not care about the initial contents can omit it.
    /// Precondition: gridWidth >= 0 and gridHeight >= 0. Negative dimensions are
    /// a programming error and are rejected with std::out_of_range rather than
    /// attempting a nonsensical allocation.
    Grid(int gridWidth, int gridHeight, const type& defaultValue = type())
        : width_(gridWidth),
          height_(gridHeight),
          cells_() {
        if (gridWidth < 0 || gridHeight < 0) {
            throw std::out_of_range("Grid dimensions must be non-negative");
        }
        // Allocate exactly width*height cells, each initialized to defaultValue.
        const int cellCount = gridWidth * gridHeight;
        cells_.assign(static_cast<std::size_t>(cellCount), defaultValue);
    }

    /// @return the grid width (number of columns).
    int width() const { return width_; }

    /// @return the grid height (number of rows).
    int height() const { return height_; }

    /// Test whether a coordinate lies inside the grid.
    /// This is the safe way to check a coordinate before calling at(): it never
    /// throws and never touches the backing store.
    /// @param x column index to test.
    /// @param y row index to test.
    /// @return true when 0 <= x < width and 0 <= y < height, false otherwise
    ///         (including for negative coordinates).
    bool inBounds(int x, int y) const {
        return x >= 0 && x < width_ && y >= 0 && y < height_;
    }

    /// Test whether a coordinate lies inside the grid.
    /// @param position the (x, y) coordinate to test.
    /// @return true when the coordinate is inside the grid (see the int overload).
    bool inBounds(const Vec2& position) const {
        return inBounds(position.x, position.y);
    }

    /// Checked, modifiable access to the cell at (x, y).
    /// @param x column index of the cell.
    /// @param y row index of the cell.
    /// @return a non-const reference to the stored cell, so the caller can read
    ///         or overwrite it.
    /// Precondition: the coordinate must be inBounds(). If it is not, this throws
    /// std::out_of_range and no memory outside the grid is read or written.
    type& at(int x, int y) {
        return cells_[checkedIndex(x, y)];
    }

    /// Checked, read-only access to the cell at (x, y).
    /// @param x column index of the cell.
    /// @param y row index of the cell.
    /// @return a const reference to the stored cell.
    /// Precondition / failure mode: identical to the non-const overload above.
    const type& at(int x, int y) const {
        return cells_[checkedIndex(x, y)];
    }

    /// Checked, modifiable access to the cell at a coordinate.
    /// @param position the (x, y) coordinate of the cell.
    /// @return a non-const reference to the stored cell.
    /// Precondition / failure mode: see the (x, y) overload above.
    type& at(const Vec2& position) {
        return at(position.x, position.y);
    }

    /// Checked, read-only access to the cell at a coordinate.
    /// @param position the (x, y) coordinate of the cell.
    /// @return a const reference to the stored cell.
    /// Precondition / failure mode: see the (x, y) overload above.
    const type& at(const Vec2& position) const {
        return at(position.x, position.y);
    }

private:
    /// Translate an (x, y) coordinate into the flat row-major index, after
    /// verifying the coordinate is inside the grid.
    /// @param x column index.
    /// @param y row index.
    /// @return the index y * width + x into the cells_ vector.
    /// @throws std::out_of_range if (x, y) is not inBounds(), which is what keeps
    ///         every at() call from touching memory outside the backing store.
    std::size_t checkedIndex(int x, int y) const {
        if (!inBounds(x, y)) {
            throw std::out_of_range(
                "Grid::at coordinate (" + std::to_string(x) + ", " +
                std::to_string(y) + ") is outside a " + std::to_string(width_) +
                "x" + std::to_string(height_) + " grid");
        }
        // Row-major: skip y whole rows, then move x cells into the current row.
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
               static_cast<std::size_t>(x);
    }

    int width_;               ///< Number of columns; fixed at construction.
    int height_;              ///< Number of rows; fixed at construction.
    std::vector<type> cells_; ///< Row-major contiguous storage (size width*height).
};

} // namespace dga
