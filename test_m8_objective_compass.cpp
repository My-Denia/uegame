// Table-driven gate for objective ordering and exact camera-relative sectors.
#include "m8_objective_compass.hpp"
#include "m8_grid_route.hpp"
#include "dungeon.hpp"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{
int failures = 0;

void check(bool condition, const std::string& name)
{
	if (!condition)
	{
		++failures;
		std::cerr << "FAIL: " << name << '\n';
	}
}

m8objective::Vec2 at_degrees(double degrees)
{
	constexpr double kDegreesToRadians = 0.01745329251994329577;
	const double radians = degrees * kDegreesToRadians;
	return { std::cos(radians), std::sin(radians) };
}
}

int main()
{
	{
		// Direct east is blocked by a solid wall; the only feasible route goes through
		// the bottom gap. This is the exact failure mode a direct compass cannot solve.
		std::vector<std::uint8_t> walkable(25, 1);
		for (int y = 0; y < 4; ++y)
		{
			walkable[static_cast<std::size_t>(y * 5 + 2)] = 0;
		}
		const std::vector<m8grid::Cell> path = m8grid::shortest_path(
			5, 5, walkable, { 0, 0 }, { 4, 0 });
		check(path.size() == 13 && path.front().x == 0 && path.front().y == 0
			&& path.back().x == 4 && path.back().y == 0,
			"grid BFS returns the shortest feasible route around a wall");
		bool path_is_valid = !path.empty();
		for (std::size_t i = 1; i < path.size(); ++i)
		{
			const int manhattan = std::abs(path[i].x - path[i - 1].x)
				+ std::abs(path[i].y - path[i - 1].y);
			path_is_valid = path_is_valid && manhattan == 1
				&& walkable[static_cast<std::size_t>(m8grid::index_of(5, path[i]))] != 0;
		}
		check(path_is_valid, "grid BFS emits only adjacent walkable cells");
		check(m8grid::shortest_path(3, 1, { 1, 0, 1 }, { 1, 0 }, { 2, 0 }).empty(),
			"wall endpoint fails closed");
		check(m8grid::shortest_path(3, 1, { 1, 1, 1 }, { -1, 0 }, { 2, 0 }).empty(),
			"out-of-bounds endpoint fails closed");
		const std::vector<m8grid::Cell> same_cell = m8grid::shortest_path(
			2, 2, { 1, 1, 1, 1 }, { 1, 1 }, { 1, 1 });
		check(same_cell.size() == 1 && same_cell.front().x == 1 && same_cell.front().y == 1,
			"start equals goal returns one cell");

		const std::vector<m8grid::Cell> tied = m8grid::shortest_path(
			2, 2, { 1, 1, 1, 1 }, { 0, 0 }, { 1, 1 });
		check(tied.size() == 3 && tied[1].x == 1 && tied[1].y == 0,
			"equal-length tie uses stable east-south-west-north expansion");

		check(m8grid::world_to_cell(0.0, 199.999, 0.0, 0.0, 200.0).x == 0
			&& m8grid::world_to_cell(0.0, 199.999, 0.0, 0.0, 200.0).y == 0,
			"world mapping keeps points below the positive tile boundary in cell zero");
		check(m8grid::world_to_cell(200.0, 400.0, 0.0, 0.0, 200.0).x == 1
			&& m8grid::world_to_cell(200.0, 400.0, 0.0, 0.0, 200.0).y == 2,
			"world mapping advances exactly on positive tile boundaries");
		check(m8grid::world_to_cell(-0.001, -200.0, 0.0, 0.0, 200.0).x == -1
			&& m8grid::world_to_cell(-0.001, -200.0, 0.0, 0.0, 200.0).y == -1,
			"world mapping floors negative coordinates");
		check(m8grid::world_to_cell(0.0, 0.0, 0.0, 0.0, 0.0).x == -1,
			"invalid tile size fails closed");
		check(m8grid::is_exact_positive_integer_step(200.0),
			"integral world step is eligible for built route identity");
		check(!m8grid::is_exact_positive_integer_step(200.5)
				&& !m8grid::is_exact_positive_integer_step(0.0),
			"fractional and non-positive world steps cannot publish route identity");

		const std::vector<m8grid::Cell> east_path{ { 37, 16 }, { 38, 16 }, { 39, 16 } };
		const m8grid::WaypointCandidates candidates =
			m8grid::ordered_waypoint_candidates(east_path);
		check(candidates.has_primary && candidates.primary.x == 38 && candidates.primary.y == 16
			&& candidates.has_fallback && candidates.fallback.x == 37
			&& candidates.fallback.y == 16,
			"candidate order tries immediate successor then current centre");
		check(m8grid::choose_clear_candidate(candidates, true, true)
				== m8grid::CandidateChoice::Primary,
			"clear immediate successor wins without recentering");
		check(m8grid::choose_clear_candidate(candidates, false, true)
				== m8grid::CandidateChoice::Fallback,
			"blocked corner uses clear current-cell centre");
		check(m8grid::choose_clear_candidate(candidates, false, false)
				== m8grid::CandidateChoice::Blocked,
			"two blocked local sweeps fail closed");
		check(m8grid::choose_clear_candidate({}, true, true)
				== m8grid::CandidateChoice::Blocked,
			"missing built route identity has no candidate");

		dungeon::Config fixed_config;
		fixed_config.seed = 9706775287700491060ULL;
		const dungeon::Layout fixed_layout = dungeon::generate(fixed_config);
		std::vector<std::uint8_t> fixed_walkable;
		fixed_walkable.reserve(fixed_layout.grid.size());
		for (const dungeon::Tile tile : fixed_layout.grid)
		{
			fixed_walkable.push_back(dungeon::isPassable(tile) ? 1u : 0u);
		}
		const dungeon::Room& fixed_start_room = fixed_layout.rooms[fixed_layout.startRoom];
		const dungeon::Room& fixed_goal_room = fixed_layout.rooms[1];
		const std::vector<m8grid::Cell> fixed_path = m8grid::shortest_path(
			fixed_config.width, fixed_config.height, fixed_walkable,
			{ fixed_start_room.cx(), fixed_start_room.cy() },
			{ fixed_goal_room.cx(), fixed_goal_room.cy() });
		check(fixed_path.size() == 58,
			"blocked Recast seed retains exact 58-cell deterministic grid route");
		check(m8grid::path_hash(fixed_path) == 0x00daa157146cc810ULL,
			"blocked Recast seed retains exact deterministic route hash");
	}

	check(m8objective::room_risk_priority(1) < m8objective::room_risk_priority(2)
		&& m8objective::room_risk_priority(2) < m8objective::room_risk_priority(3),
		"auto room guidance orders Standard before Skirmish before Stronghold");
	check(m8objective::room_risk_priority(-1) > m8objective::room_risk_priority(3),
		"invalid room role fails closed behind valid combat roles");
	using namespace m8objective;

	check(select_best({ { 9.0, 1, 9, true }, { 4.0, 9, 1, true } }) == 1,
		"nearest distance wins");
	check(select_best({ { 4.0, 8, 1, true }, { 4.0, 3, 9, true } }) == 1,
		"equal distance lower valid ordinal wins");
	check(select_best({ { 4.0, 0, 1, false }, { 4.0, 99, 99, true } }) == 1,
		"valid ordinal precedes invalid");
	check(select_best({ { 4.0, 0, 8, false }, { 4.0, 0, 2, false } }) == 1,
		"invalid ordinal uses fallback id");
	check(select_best({}) == kNoCandidate, "empty candidate sentinel");

	struct DirectionRow { double degrees; Direction8 expected; const char* name; };
	const DirectionRow centers[] = {
		{ 0.0, Direction8::Ahead, "Ahead" },
		{ 45.0, Direction8::AheadRight, "Ahead-Right" },
		{ 90.0, Direction8::Right, "Right" },
		{ 135.0, Direction8::BehindRight, "Behind-Right" },
		{ 180.0, Direction8::Behind, "Behind" },
		{ -135.0, Direction8::BehindLeft, "Behind-Left" },
		{ -90.0, Direction8::Left, "Left" },
		{ -45.0, Direction8::AheadLeft, "Ahead-Left" }
	};
	for (const DirectionRow& row : centers)
	{
		const Direction8 actual = map_direction(at_degrees(row.degrees), { 1.0, 0.0 }, { 0.0, 1.0 });
		check(actual == row.expected, std::string("sector center ") + row.name);
		check(std::string(direction_name(actual)) == row.name, std::string("sector label ") + row.name);
	}

	const DirectionRow boundaries[] = {
		{ 22.5, Direction8::AheadRight, "+22.5 clockwise" },
		{ -22.5, Direction8::Ahead, "-22.5 clockwise" },
		{ 67.5, Direction8::Right, "+67.5 clockwise" },
		{ -67.5, Direction8::AheadLeft, "-67.5 clockwise" },
		{ 112.5, Direction8::BehindRight, "+112.5 clockwise" },
		{ -112.5, Direction8::Left, "-112.5 clockwise" },
		{ 157.5, Direction8::Behind, "+157.5 clockwise" },
		{ -157.5, Direction8::BehindLeft, "-157.5 clockwise" }
	};
	for (const DirectionRow& row : boundaries)
	{
		check(map_direction(at_degrees(row.degrees), { 1.0, 0.0 }, { 0.0, 1.0 }) == row.expected,
			std::string("boundary ") + row.name);
	}

	check(map_direction({ 0.0, 1.0 }, { 0.0, 0.0 }, { 0.0, 1.0 }) == Direction8::Ahead,
		"degenerate camera uses player-forward fallback");
	check(map_direction({ 1.0, 0.0 }, { 0.0, 0.0 }, { 0.0, 0.0 }) == Direction8::Ahead,
		"double-degenerate forward uses world +X");
	check(map_direction({ 0.0, 0.0 }, { 0.0, 1.0 }, { 1.0, 0.0 }) == Direction8::Ahead,
		"degenerate target delta maps ahead");

	const Vec2 Pawn{ 10.0, 20.0 };
	const Vec2 Target{ 900.0, 700.0 };
	WaypointChoice Choice = select_route_waypoint(
		{ { 10.0, 20.0 }, { 159.0, 20.0 }, { 160.0, 20.0 }, { 161.0, 20.0 } },
		Pawn, Target, 150.0);
	check(!Choice.used_target_fallback && Choice.path_index == 3
			&& Choice.waypoint.x == 161.0,
		"waypoint rejects below and exact 150 then selects first above");

	Choice = select_route_waypoint(
		{ { 10.0, 20.0 }, { 170.0, 20.0 }, { 400.0, 20.0 } },
		Pawn, Target, 150.0);
	check(!Choice.used_target_fallback && Choice.path_index == 1
			&& Choice.waypoint.x == 170.0,
		"later path point cannot preempt first qualifying waypoint");

	Choice = select_route_waypoint(
		{ { 10.0, 20.0 }, { 100.0, 20.0 }, { 160.0, 20.0 } },
		Pawn, Target, 150.0);
	check(Choice.used_target_fallback && Choice.path_index == kNoCandidate
			&& Choice.waypoint.x == Target.x && Choice.waypoint.y == Target.y,
		"complete path with no point above threshold falls back to live target");

	Choice = select_route_waypoint(
		{ { 10.0, 20.0 }, { 175.0, 35.0 }, { 240.0, 40.0 } },
		Pawn, Target, 150.0);
	check(!Choice.used_target_fallback && Choice.path_index == 1
			&& Choice.waypoint.x == 175.0 && Choice.waypoint.y == 35.0,
		"reachable path prefix supplies the first qualifying waypoint");

	check(route_query_allowed(10.0, {}), "first route query allowed");
	check(!route_query_allowed(10.199, { 10.0 }), "global minimum interval rejects early query");
	check(route_query_allowed(10.2, { 10.0 }), "exact global minimum interval allowed");
	check(!route_query_allowed(10.9, { 10.0, 10.2, 10.4, 10.6, 10.8 }),
		"rolling half-open window rejects sixth query");
	check(route_query_allowed(11.000001, { 10.0, 10.2, 10.4, 10.6, 10.8 }),
		"rolling half-open window admits query after oldest timestamp expires");

	if (failures == 0)
	{
		std::cout << "PASS: objective ordering, direction, waypoint, and throttle matrices\n";
	}
	return failures == 0 ? 0 : 1;
}
