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

bool canonicalize_unique(const std::vector<std::int64_t>& input,
	std::vector<std::int64_t>& output)
{
	output = input;
	std::sort(output.begin(), output.end());
	return std::adjacent_find(output.begin(), output.end()) == output.end();
}

bool exact_unique_set(const std::vector<std::int64_t>& lhs,
	const std::vector<std::int64_t>& rhs)
{
	std::vector<std::int64_t> canonical_lhs;
	std::vector<std::int64_t> canonical_rhs;
	return canonicalize_unique(lhs, canonical_lhs)
		&& canonicalize_unique(rhs, canonical_rhs)
		&& canonical_lhs == canonical_rhs;
}

TransactionTransition commit(const ContractTransaction& current, Choice choice,
	TransactionAction action)
{
	TransactionTransition out;
	out.state = current;
	out.state.phase = TransactionPhase::Committed;
	out.state.choice = choice;
	out.state.challenge_enabled = choice == Choice::Challenge;
	out.state.world_paused = false;
	out.state.commit_count = current.commit_count + 1;
	out.state.expected_ids.clear();
	out.action = action;
	return out;
}

TransactionTransition secure_fallback(const ContractTransaction& current)
{
	TransactionTransition out = commit(current, Choice::Secure,
		TransactionAction::CommitSecure);
	out.state.challenge_enabled = false;
	return out;
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

ContractTransaction begin_transaction(bool contract_available)
{
	ContractTransaction out;
	if (contract_available)
	{
		out.phase = TransactionPhase::Pending;
		out.choice = Choice::Pending;
		out.challenge_enabled = true;
		out.world_paused = true;
	}
	return out;
}

TransactionTransition choose_secure(const ContractTransaction& current)
{
	if (current.phase != TransactionPhase::Pending)
	{
		return {current, TransactionAction::None};
	}
	return commit(current, Choice::Secure, TransactionAction::CommitSecure);
}

TransactionTransition preflight_challenge(const ContractTransaction& current,
	const std::vector<std::int64_t>& expected_ids,
	const std::vector<std::int64_t>& eligible_ids,
	bool target_is_current)
{
	if (current.phase != TransactionPhase::Pending)
	{
		return {current, TransactionAction::None};
	}

	std::vector<std::int64_t> canonical_expected;
	const bool valid_expected = !expected_ids.empty()
		&& canonicalize_unique(expected_ids, canonical_expected);
	if (!current.challenge_enabled || !target_is_current || !valid_expected
		|| !exact_unique_set(expected_ids, eligible_ids))
	{
		return secure_fallback(current);
	}

	TransactionTransition out;
	out.state = current;
	out.state.phase = TransactionPhase::ApplyingChallenge;
	out.state.expected_ids = canonical_expected;
	out.action = TransactionAction::ApplyChallenge;
	return out;
}

TransactionTransition finalize_challenge(const ContractTransaction& current,
	const std::vector<std::int64_t>& applied_ids,
	const std::vector<std::int64_t>& rolled_back_ids)
{
	if (current.phase != TransactionPhase::ApplyingChallenge)
	{
		return {current, TransactionAction::None};
	}

	if (rolled_back_ids.empty()
		&& exact_unique_set(current.expected_ids, applied_ids))
	{
		return commit(current, Choice::Challenge,
			TransactionAction::CommitChallenge);
	}

	if (exact_unique_set(applied_ids, rolled_back_ids))
	{
		return secure_fallback(current);
	}

	TransactionTransition out;
	out.state = current;
	out.state.phase = TransactionPhase::Error;
	out.state.choice = Choice::Unavailable;
	out.state.challenge_enabled = false;
	out.state.world_paused = false;
	out.state.expected_ids.clear();
	out.action = TransactionAction::SignalError;
	return out;
}
}
