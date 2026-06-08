// Pathfinder.cpp — BFS shortest path on the dungeon grid.

#include "world/Pathfinder.h"

#include <algorithm>
#include <queue>

#include "core/Direction.h"
#include "core/Grid.h"
#include "world/GridMap.h"

namespace dga {
namespace {

constexpr int kUnvisited = -1;
constexpr int kStartDistance = 0;

bool isBlocked(const Vec2& tile, const std::vector<Vec2>& blockedTiles) {
    return std::find(blockedTiles.begin(), blockedTiles.end(), tile) !=
           blockedTiles.end();
}

} // namespace

std::vector<Vec2> Pathfinder::findPath(
    const GridMap& map,
    const Vec2& start,
    const Vec2& goal,
    const std::vector<Vec2>& blockedTiles) const {

    if (!map.isWalkable(start) || !map.isWalkable(goal)) {
        return {};
    }
    if (start == goal) {
        return {start};
    }

    // BFS distance buffer; every cell starts at kUnvisited.
    Grid<int> distances(map.width(), map.height(), kUnvisited);
    std::queue<Vec2> frontier;

    distances.at(start) = kStartDistance;
    frontier.push(start);

    while (!frontier.empty()) {
        const Vec2 current = frontier.front();
        frontier.pop();

        if (current == goal) {
            break;
        }

        const int nextDistance = distances.at(current) + 1;

        for (const Direction direction : allDirections()) {
            const Vec2 neighbour = current + toOffset(direction);

            const bool walkable = map.isWalkable(neighbour);
            const bool free     = (neighbour == goal) ||
                                  !isBlocked(neighbour, blockedTiles);
            const bool unseen   = distances.at(neighbour) == kUnvisited;

            if (walkable && free && unseen) {
                distances.at(neighbour) = nextDistance;
                frontier.push(neighbour);
            }
        }
    }

    if (distances.at(goal) == kUnvisited) {
        return {}; // No path.
    }

    // Walk the distance field backwards from the goal: at every step move to a
    // neighbour with distance exactly one less. This reconstructs a shortest path.
    std::vector<Vec2> path;
    Vec2 trace = goal;
    path.push_back(trace);

    while (distances.at(trace) != kStartDistance) {
        const int targetDistance = distances.at(trace) - 1;
        for (const Direction direction : allDirections()) {
            const Vec2 neighbour = trace + toOffset(direction);
            if (distances.inBounds(neighbour) &&
                distances.at(neighbour) == targetDistance) {
                trace = neighbour;
                path.push_back(trace);
                break;
            }
        }
    }

    std::reverse(path.begin(), path.end());
    return path;
}

Vec2 Pathfinder::nextStepToward(const GridMap& map,
                                const Vec2& start,
                                const Vec2& goal,
                                const std::vector<Vec2>& blockedTiles) const {
    const std::vector<Vec2> path = findPath(map, start, goal, blockedTiles);
    if (path.size() < 2) {
        return start; // No path, or already on the goal.
    }
    return path[1];
}

} // namespace dga
