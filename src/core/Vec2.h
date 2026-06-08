// =============================================================================
// core/Vec2.h
//
// Purpose:
//   Vec2 is a tiny, immutable-feeling value type that holds a pair of integer
//   grid coordinates (a column x and a row y). It is the fundamental way the
//   whole game addresses a single cell on the dungeon grid and expresses
//   movement offsets / directions as small integer vectors.
//
// Why header-only:
//   Vec2 is a trivial value type with no hidden logic, so (per the project's
//   multi-file rule, which allows small value types to live in a header) all of
//   its definitions are inline in this header. There is no Vec2.cpp.
//
// Layer: core (depends on nothing).
// =============================================================================
#pragma once

#include <cstdlib> // std::abs - used for the distance helpers below.

namespace dga {

/// A 2D integer grid coordinate or offset.
///
/// Conventionally `x` is the column (horizontal axis) and `y` is the row
/// (vertical axis). The same type doubles as a direction/offset vector, for
/// example Vec2{1, 0} means "one cell to the right".
struct Vec2 {
    int x; ///< Column index (horizontal position on the grid).
    int y; ///< Row index (vertical position on the grid).

    /// Construct a coordinate or offset.
    /// @param columnX horizontal component (defaults to 0).
    /// @param rowY    vertical component (defaults to 0).
    /// Defaulting both to 0 lets `Vec2 v;` create the origin (0, 0).
    explicit Vec2(int columnX = 0, int rowY = 0) : x(columnX), y(rowY) {}

    /// Vector addition: step from this coordinate by an offset.
    /// @param other the offset (or coordinate) to add component-wise.
    /// @return a new Vec2 whose components are the sums of the two operands.
     Vec2 operator+(const Vec2& other) const {
        return Vec2(x + other.x, y + other.y);
    }

    /// Vector subtraction: the offset that goes from `other` to this point.
    /// @param other the coordinate (or offset) to subtract component-wise.
    /// @return a new Vec2 whose components are the differences of the operands.
     Vec2 operator-(const Vec2& other) const {
        return Vec2(x - other.x, y - other.y);
    }

    /// Equality test, used for visited-set / position checks.
    /// @param other the coordinate to compare against.
    /// @return true when both components are equal.
     bool operator==(const Vec2& other) const {
        return x == other.x && y == other.y;
    }

    /// Inequality test (logical negation of operator==).
    /// @param other the coordinate to compare against.
    /// @return true when either component differs.
     bool operator!=(const Vec2& other) const {
        return !(*this == other);
    }

    /// Manhattan (taxicab) distance between this coordinate and another.
    /// This counts orthogonal steps and is used by range checks for the
    /// default 4-directional movement model (OD-2).
    /// @param other the other grid coordinate.
    /// @return |dx| + |dy|, the number of orthogonal moves between the cells.
    int manhattan(const Vec2& other) const {
        return std::abs(x - other.x) + std::abs(y - other.y);
    }

    /// Chebyshev (chessboard) distance between this coordinate and another.
    /// This is the larger of the two axis distances and is used for adjacency
    /// and the Nova ability's "ring of surrounding cells" check.
    /// @param other the other grid coordinate.
    /// @return max(|dx|, |dy|), treating a diagonal step as distance 1.
    int chebyshev(const Vec2& other) const {
        const int deltaX = std::abs(x - other.x);
        const int deltaY = std::abs(y - other.y);
        return deltaX > deltaY ? deltaX : deltaY;
    }
};

} // namespace dga
