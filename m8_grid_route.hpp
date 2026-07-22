// m8_grid_route.hpp - deterministic shortest feasible route over the frozen M1 walkable grid.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace m8grid
{
struct Cell
{
	int x = 0;
	int y = 0;
};

struct FeasibleWaypoint
{
	double x = 0.0;
	double y = 0.0;
	bool recenter = false;
};

inline int index_of(int width, const Cell& cell)
{
	return cell.y * width + cell.x;
}

inline bool valid_cell(int width, int height, const Cell& cell)
{
	return cell.x >= 0 && cell.y >= 0 && cell.x < width && cell.y < height;
}

inline Cell nearest_walkable(
	int width,
	int height,
	const std::vector<std::uint8_t>& walkable,
	const Cell& requested)
{
	Cell best{ -1, -1 };
	long long best_distance = std::numeric_limits<long long>::max();
	int best_index = std::numeric_limits<int>::max();
	for (int y = 0; y < height; ++y)
	{
		for (int x = 0; x < width; ++x)
		{
			const Cell candidate{ x, y };
			const int candidate_index = index_of(width, candidate);
			if (candidate_index < 0
				|| static_cast<std::size_t>(candidate_index) >= walkable.size()
				|| walkable[static_cast<std::size_t>(candidate_index)] == 0)
			{
				continue;
			}
			const long long dx = static_cast<long long>(x) - requested.x;
			const long long dy = static_cast<long long>(y) - requested.y;
			const long long distance = dx * dx + dy * dy;
			if (distance < best_distance || (distance == best_distance && candidate_index < best_index))
			{
				best = candidate;
				best_distance = distance;
				best_index = candidate_index;
			}
		}
	}
	return best;
}

inline std::vector<Cell> shortest_path(
	int width,
	int height,
	const std::vector<std::uint8_t>& walkable,
	const Cell& requested_start,
	const Cell& requested_goal)
{
	if (width <= 0 || height <= 0
		|| walkable.size() != static_cast<std::size_t>(width * height))
	{
		return {};
	}
	const Cell start = nearest_walkable(width, height, walkable, requested_start);
	const Cell goal = nearest_walkable(width, height, walkable, requested_goal);
	if (!valid_cell(width, height, start) || !valid_cell(width, height, goal))
	{
		return {};
	}

	const int start_index = index_of(width, start);
	const int goal_index = index_of(width, goal);
	std::vector<int> predecessor(walkable.size(), -1);
	std::vector<int> queue;
	queue.reserve(walkable.size());
	predecessor[static_cast<std::size_t>(start_index)] = start_index;
	queue.push_back(start_index);
	constexpr int kDx[4] = { 1, 0, -1, 0 };
	constexpr int kDy[4] = { 0, 1, 0, -1 };
	for (std::size_t head = 0; head < queue.size(); ++head)
	{
		const int current = queue[head];
		if (current == goal_index)
		{
			break;
		}
		const Cell current_cell{ current % width, current / width };
		for (int direction = 0; direction < 4; ++direction)
		{
			const Cell next{ current_cell.x + kDx[direction], current_cell.y + kDy[direction] };
			if (!valid_cell(width, height, next))
			{
				continue;
			}
			const int next_index = index_of(width, next);
			if (walkable[static_cast<std::size_t>(next_index)] == 0
				|| predecessor[static_cast<std::size_t>(next_index)] != -1)
			{
				continue;
			}
			predecessor[static_cast<std::size_t>(next_index)] = current;
			queue.push_back(next_index);
		}
	}
	if (predecessor[static_cast<std::size_t>(goal_index)] == -1)
	{
		return {};
	}

	std::vector<Cell> reversed;
	for (int current = goal_index;; current = predecessor[static_cast<std::size_t>(current)])
	{
		reversed.push_back({ current % width, current / width });
		if (current == start_index)
		{
			break;
		}
	}
	return std::vector<Cell>(reversed.rbegin(), reversed.rend());
}

// Convert the next four-neighbour BFS step into a capsule-friendly polyline waypoint.
// A pawn near a side wall cannot safely cut diagonally toward the next cell centre: its
// centre ray is clear while its collision radius clips the door corner. Use the fixed current
// cell centre as a stable clearance waypoint before advancing. A moving projection can flip
// sides while the player is still holding input and produce oscillating guidance.
inline FeasibleWaypoint next_feasible_waypoint(
	const std::vector<Cell>& path,
	double start_grid_x,
	double start_grid_y,
	double centerline_tolerance_cells = 0.20)
{
	if (path.empty())
	{
		return {};
	}
	const Cell& current = path[0];
	const double current_center_x = static_cast<double>(current.x) + 0.5;
	const double current_center_y = static_cast<double>(current.y) + 0.5;
	if (path.size() == 1)
	{
		return { current_center_x, current_center_y, false };
	}
	const Cell& next = path[1];
	if (next.x != current.x
		&& std::abs(start_grid_y - current_center_y) > centerline_tolerance_cells)
	{
		return { current_center_x, current_center_y, true };
	}
	if (next.y != current.y
		&& std::abs(start_grid_x - current_center_x) > centerline_tolerance_cells)
	{
		return { current_center_x, current_center_y, true };
	}
	return {
		static_cast<double>(next.x) + 0.5,
		static_cast<double>(next.y) + 0.5,
		false
	};
}
}
