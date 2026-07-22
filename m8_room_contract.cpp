#include "m8_room_contract.hpp"

#include <algorithm>
#include <limits>

namespace m8contract
{
namespace
{
bool valid(const RoomCandidate& room)
{
	const int role = static_cast<int>(room.role);
	return room.room_index >= 0 && !room.is_start && room.initial_enemies > 0
		&& role >= static_cast<int>(Role::Quiet)
		&& role <= static_cast<int>(Role::Stronghold)
		&& room.distance_squared >= 0.0;
}

bool secure_preferred(Role role)
{
	return role == Role::Quiet || role == Role::Standard;
}

bool challenge_preferred(Role role)
{
	return role == Role::Skirmish || role == Role::Stronghold;
}

int choose_min(const std::vector<RoomCandidate>& rooms, bool preferred_only)
{
	const RoomCandidate* best = nullptr;
	for (const RoomCandidate& room : rooms)
	{
		if (!valid(room) || (preferred_only && !secure_preferred(room.role)))
		{
			continue;
		}
		if (!best || room.distance_squared < best->distance_squared
			|| (room.distance_squared == best->distance_squared && room.room_index < best->room_index))
		{
			best = &room;
		}
	}
	return best ? best->room_index : -1;
}

int choose_max(const std::vector<RoomCandidate>& rooms, bool preferred_only)
{
	const RoomCandidate* best = nullptr;
	for (const RoomCandidate& room : rooms)
	{
		if (!valid(room) || (preferred_only && !challenge_preferred(room.role)))
		{
			continue;
		}
		if (!best || room.distance_squared > best->distance_squared
			|| (room.distance_squared == best->distance_squared && room.room_index > best->room_index))
		{
			best = &room;
		}
	}
	return best ? best->room_index : -1;
}
}

Selection select_rooms(const std::vector<RoomCandidate>& rooms)
{
	Selection out;
	out.secure_room = choose_min(rooms, true);
	if (out.secure_room < 0)
	{
		out.secure_room = choose_min(rooms, false);
	}
	out.challenge_room = choose_max(rooms, true);
	if (out.challenge_room < 0)
	{
		out.challenge_room = choose_max(rooms, false);
	}
	out.available = out.secure_room >= 0 && out.challenge_room >= 0;
	return out;
}

Dispatch dispatch_slot(bool pause_menu_active, bool contract_pending,
	bool reward_pending, int slot_index)
{
	if (pause_menu_active)
	{
		return { InputOwner::PauseMenu, false };
	}
	if (contract_pending)
	{
		return { InputOwner::Contract, slot_index == 0 || slot_index == 1 };
	}
	if (reward_pending)
	{
		return { InputOwner::Reward, slot_index >= 0 && slot_index <= 2 };
	}
	return {};
}

int challenge_damage(int committed_damage)
{
	if (committed_damage <= 0)
	{
		return 0;
	}
	const std::int64_t scaled = static_cast<std::int64_t>(committed_damage) * 120 + 50;
	return static_cast<int>(std::min<std::int64_t>(scaled / 100, std::numeric_limits<int>::max()));
}

std::int64_t challenge_phase_ms(std::int64_t base_ms)
{
	if (base_ms <= 0)
	{
		return 0;
	}
	return (base_ms * 5 + 3) / 6;
}

bool can_complete_floor(bool already_complete, int cleared_rooms, int required_rooms,
	Choice choice, bool selected_room_cleared)
{
	if (already_complete || required_rooms < 0 || cleared_rooms < required_rooms)
	{
		return false;
	}
	if (choice == Choice::Secure || choice == Choice::Challenge)
	{
		return selected_room_cleared;
	}
	return choice == Choice::Unavailable;
}
}
