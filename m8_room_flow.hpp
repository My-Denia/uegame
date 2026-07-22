// m8_room_flow.hpp - RNG-free room-clear and post-pick recovery rules.
// Engine-independent and additive: no M1-M7 core includes or state.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace m8room
{
enum class Role : int { Quiet = 0, Standard = 1, Skirmish = 2, Stronghold = 3 };

struct Config
{
	std::array<double, 4> room_clear_fraction{{0.0, 0.03, 0.06, 0.10}};
	double reward_floor_fraction = 0.60;
	int required_combat_rooms_base = 5;
	int required_combat_rooms_per_floor = 1;
};

struct Recovery
{
	double before = 0.0;
	double after = 0.0;
	double restored = 0.0;
};

inline bool valid_fraction(double value)
{
	return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

inline bool validate(const Config& config)
{
	for (const double fraction : config.room_clear_fraction)
	{
		if (!valid_fraction(fraction)) { return false; }
	}
	constexpr int max_room_quota_setting = 1024;
	return valid_fraction(config.reward_floor_fraction)
		&& config.required_combat_rooms_base >= 0
		&& config.required_combat_rooms_base <= max_room_quota_setting
		&& config.required_combat_rooms_per_floor >= 0
		&& config.required_combat_rooms_per_floor <= max_room_quota_setting;
}

inline int required_combat_rooms(
	int floor_index,
	int max_floors,
	int actual_combat_rooms,
	const Config& config)
{
	if (!validate(config) || floor_index <= 0 || max_floors <= 0 || actual_combat_rooms <= 0)
	{
		return 0;
	}
	if (floor_index >= max_floors)
	{
		return actual_combat_rooms;
	}

	// The settings are bounded by validate(), but the floor index is external. Compute in
	// int64 and saturate before narrowing so even a corrupt INT_MAX floor cannot overflow.
	const std::int64_t floor_offset = static_cast<std::int64_t>(floor_index - 1);
	const std::int64_t desired = std::min<std::int64_t>(
		static_cast<std::int64_t>(config.required_combat_rooms_base)
			+ floor_offset * static_cast<std::int64_t>(config.required_combat_rooms_per_floor),
		static_cast<std::int64_t>(std::numeric_limits<int>::max()));
	return std::min(actual_combat_rooms, static_cast<int>(desired));
}

inline bool objective_complete(int cleared_combat_rooms, int required_combat_rooms_value)
{
	return required_combat_rooms_value >= 0
		&& cleared_combat_rooms >= required_combat_rooms_value;
}

inline bool should_transition_to_complete(
	bool was_complete,
	int cleared_combat_rooms,
	int required_combat_rooms_value)
{
	return !was_complete && objective_complete(cleared_combat_rooms, required_combat_rooms_value);
}

inline bool should_neutralize_optional_threats(
	bool first_transition_to_complete,
	int floor_index,
	int max_floors,
	int cleared_combat_rooms,
	int actual_combat_rooms)
{
	return first_transition_to_complete
		&& floor_index > 0
		&& max_floors > 0
		&& floor_index < max_floors
		&& cleared_combat_rooms >= 0
		&& actual_combat_rooms > cleared_combat_rooms;
}

inline bool is_first_true_clear(int old_alive, int new_alive)
{
	return old_alive > 0 && new_alive == 0;
}

inline double room_fraction(Role role, const Config& config)
{
	const int index = static_cast<int>(role);
	return validate(config) && index >= 0 && index < static_cast<int>(config.room_clear_fraction.size())
		? config.room_clear_fraction[static_cast<std::size_t>(index)] : 0.0;
}

inline Recovery apply_room_clear(Role role, double current_hp, double max_hp, const Config& config)
{
	if (!std::isfinite(max_hp) || max_hp <= 0.0) { return {}; }
	const double before = std::clamp(std::isfinite(current_hp) ? current_hp : 0.0, 0.0, max_hp);
	if (before <= 0.0 || before >= max_hp) { return {before, before, 0.0}; }
	const double after = std::clamp(before + max_hp * room_fraction(role, config), 0.0, max_hp);
	return {before, after, after - before};
}

inline Recovery apply_reward_floor(double current_hp, double max_hp, const Config& config)
{
	if (!std::isfinite(max_hp) || max_hp <= 0.0) { return {}; }
	const double before = std::clamp(std::isfinite(current_hp) ? current_hp : 0.0, 0.0, max_hp);
	if (before <= 0.0 || !validate(config)) { return {before, before, 0.0}; }
	const double after = std::clamp(std::max(before, max_hp * config.reward_floor_fraction), 0.0, max_hp);
	return {before, after, after - before};
}
} // namespace m8room
