// m8_objective_compass.hpp - pure ordering and camera-relative direction rules for the live HUD.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace m8objective
{
inline int room_risk_priority(int role_id)
{
	switch (role_id)
	{
	case 1: return 0; // Standard
	case 2: return 1; // Skirmish
	case 3: return 2; // Stronghold
	case 0: return 3; // Quiet normally has no live enemies
	default: return 4;
	}
}

struct CandidateKey
{
	double distance_squared = 0.0;
	std::uint32_t spawn_ordinal = 0;
	std::uint32_t fallback_id = 0;
	bool ordinal_valid = false;
};

inline bool candidate_less(const CandidateKey& a, const CandidateKey& b)
{
	if (a.distance_squared != b.distance_squared)
	{
		return a.distance_squared < b.distance_squared;
	}
	if (a.ordinal_valid != b.ordinal_valid)
	{
		return a.ordinal_valid;
	}
	if (a.ordinal_valid)
	{
		return a.spawn_ordinal < b.spawn_ordinal;
	}
	return a.fallback_id < b.fallback_id;
}

constexpr std::size_t kNoCandidate = static_cast<std::size_t>(-1);

inline std::size_t select_best(const std::vector<CandidateKey>& candidates)
{
	if (candidates.empty())
	{
		return kNoCandidate;
	}
	std::size_t best = 0;
	for (std::size_t i = 1; i < candidates.size(); ++i)
	{
		if (candidate_less(candidates[i], candidates[best]))
		{
			best = i;
		}
	}
	return best;
}

struct Vec2
{
	double x = 0.0;
	double y = 0.0;
};

enum class Direction8 : std::uint8_t
{
	Ahead = 0,
	AheadRight = 1,
	Right = 2,
	BehindRight = 3,
	Behind = 4,
	BehindLeft = 5,
	Left = 6,
	AheadLeft = 7
};

inline const char* direction_name(Direction8 direction)
{
	switch (direction)
	{
	case Direction8::Ahead:       return "Ahead";
	case Direction8::AheadRight:  return "Ahead-Right";
	case Direction8::Right:       return "Right";
	case Direction8::BehindRight: return "Behind-Right";
	case Direction8::Behind:      return "Behind";
	case Direction8::BehindLeft:  return "Behind-Left";
	case Direction8::Left:        return "Left";
	case Direction8::AheadLeft:   return "Ahead-Left";
	}
	return "Ahead";
}

inline double length_squared(const Vec2& v)
{
	return v.x * v.x + v.y * v.y;
}

inline Vec2 normalized_or(const Vec2& candidate, const Vec2& fallback)
{
	constexpr double kDegenerateSq = 1.0e-12;
	const double candidate_sq = length_squared(candidate);
	if (candidate_sq > kDegenerateSq)
	{
		const double inv_length = 1.0 / std::sqrt(candidate_sq);
		return { candidate.x * inv_length, candidate.y * inv_length };
	}
	const double fallback_sq = length_squared(fallback);
	if (fallback_sq > kDegenerateSq)
	{
		const double inv_length = 1.0 / std::sqrt(fallback_sq);
		return { fallback.x * inv_length, fallback.y * inv_length };
	}
	return { 1.0, 0.0 };
}

inline double normalize_degrees(double degrees)
{
	double normalized = std::fmod(degrees + 180.0, 360.0);
	if (normalized < 0.0)
	{
		normalized += 360.0;
	}
	return normalized - 180.0;
}

inline Direction8 map_direction(
	const Vec2& target_delta,
	const Vec2& camera_forward,
	const Vec2& fallback_forward)
{
	constexpr double kDegenerateSq = 1.0e-12;
	if (length_squared(target_delta) <= kDegenerateSq)
	{
		return Direction8::Ahead;
	}

	const Vec2 forward = normalized_or(camera_forward, fallback_forward);
	const Vec2 right{ -forward.y, forward.x };
	constexpr double kRadiansToDegrees = 57.2957795130823208768;
	const double angle = normalize_degrees(std::atan2(
		target_delta.x * right.x + target_delta.y * right.y,
		target_delta.x * forward.x + target_delta.y * forward.y) * kRadiansToDegrees);
	// Adding a tiny epsilon makes mathematically exact half-sector ties resolve clockwise.
	int sector = static_cast<int>(std::floor((angle + 22.5) / 45.0 + 1.0e-12));
	sector = ((sector % 8) + 8) % 8;
	return static_cast<Direction8>(sector);
}

struct WaypointChoice
{
	Vec2 waypoint;
	std::size_t path_index = kNoCandidate;
	bool used_target_fallback = true;
};

inline WaypointChoice select_route_waypoint(
	const std::vector<Vec2>& ordered_path,
	const Vec2& pawn,
	const Vec2& live_target,
	double minimum_distance)
{
	const double minimum_distance_squared = minimum_distance * minimum_distance;
	for (std::size_t i = 0; i < ordered_path.size(); ++i)
	{
		const Vec2 delta{
			ordered_path[i].x - pawn.x,
			ordered_path[i].y - pawn.y
		};
		if (length_squared(delta) > minimum_distance_squared)
		{
			return { ordered_path[i], i, false };
		}
	}
	return { live_target, kNoCandidate, true };
}

constexpr double kRouteQueryMinimumIntervalSeconds = 0.2;
constexpr std::size_t kRouteQueryMaximumPerSecond = 5;

inline bool route_query_allowed(
	double candidate_time,
	const std::vector<double>& prior_query_timestamps)
{
	if (!prior_query_timestamps.empty()
		&& candidate_time
			< prior_query_timestamps.back() + kRouteQueryMinimumIntervalSeconds)
	{
		return false;
	}

	std::size_t rolling_prior_count = 0;
	const double window_start = candidate_time - 1.0;
	for (double timestamp : prior_query_timestamps)
	{
		if (timestamp >= window_start && timestamp < candidate_time)
		{
			++rolling_prior_count;
		}
	}
	return rolling_prior_count < kRouteQueryMaximumPerSecond;
}
}
