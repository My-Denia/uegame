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

struct WaypointCandidates
{
	Cell primary;
	Cell fallback;
	bool has_primary = false;
	bool has_fallback = false;
};

enum class CandidateChoice : std::uint8_t
{
	Blocked = 0,
	Primary = 1,
	Fallback = 2
};

inline int index_of(int width, const Cell& cell)
{
	return cell.y * width + cell.x;
}

inline bool valid_cell(int width, int height, const Cell& cell)
{
	return cell.x >= 0 && cell.y >= 0 && cell.x < width && cell.y < height;
}

inline bool is_exact_positive_integer_step(double value)
{
	return std::isfinite(value)
		&& value > 0.0
		&& value <= static_cast<double>(std::numeric_limits<long long>::max())
		&& std::floor(value) == value;
}

inline Cell world_to_cell(
	double world_x,
	double world_y,
	double origin_x,
	double origin_y,
	double tile_size)
{
	if (!(tile_size > 0.0) || !std::isfinite(world_x) || !std::isfinite(world_y)
		|| !std::isfinite(origin_x) || !std::isfinite(origin_y) || !std::isfinite(tile_size))
	{
		return { -1, -1 };
	}
	return {
		static_cast<int>(std::floor((world_x - origin_x) / tile_size)),
		static_cast<int>(std::floor((world_y - origin_y) / tile_size))
	};
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
	if (!valid_cell(width, height, requested_start)
		|| !valid_cell(width, height, requested_goal))
	{
		return {};
	}

	const Cell start = requested_start;
	const Cell goal = requested_goal;
	const int start_index = index_of(width, start);
	const int goal_index = index_of(width, goal);
	if (walkable[static_cast<std::size_t>(start_index)] == 0
		|| walkable[static_cast<std::size_t>(goal_index)] == 0)
	{
		return {};
	}
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

// The runtime checks both segments from the pawn's ACTUAL position. Trying the immediate
// successor first avoids the oscillation caused by a tolerance-based "recentre whenever
// off-axis" rule; the current cell centre is only a fallback when the successor sweep clips
// a wall corner.
inline WaypointCandidates ordered_waypoint_candidates(const std::vector<Cell>& path)
{
	if (path.empty())
	{
		return {};
	}
	if (path.size() == 1)
	{
		return { path[0], {}, true, false };
	}
	return { path[1], path[0], true, true };
}

inline CandidateChoice choose_clear_candidate(
	const WaypointCandidates& candidates,
	bool primary_clear,
	bool fallback_clear)
{
	if (candidates.has_primary && primary_clear)
	{
		return CandidateChoice::Primary;
	}
	if (candidates.has_fallback && fallback_clear)
	{
		return CandidateChoice::Fallback;
	}
	return CandidateChoice::Blocked;
}

inline std::uint64_t path_hash(const std::vector<Cell>& path)
{
	std::uint64_t hash = 1469598103934665603ULL;
	constexpr std::uint64_t prime = 1099511628211ULL;
	auto mix_int = [&hash](int value)
	{
		const std::uint32_t bits = static_cast<std::uint32_t>(value);
		for (int shift = 0; shift < 32; shift += 8)
		{
			hash ^= static_cast<std::uint8_t>((bits >> shift) & 0xffu);
			hash *= prime;
		}
	};
	for (const Cell& cell : path)
	{
		mix_int(cell.x);
		mix_int(cell.y);
	}
	return hash;
}
}
