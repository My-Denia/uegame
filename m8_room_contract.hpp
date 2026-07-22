// m8_room_contract.hpp - RNG-free M8 2B room contract rules.
//
// UE owns pausing, actors and presentation. This core owns deterministic room selection,
// contextual key arbitration, exact challenge quantization and completion gating.

#pragma once

#include <cstdint>
#include <vector>

namespace m8contract
{
enum class Role : std::int32_t
{
	Quiet = 0,
	Standard = 1,
	Skirmish = 2,
	Stronghold = 3
};

enum class Choice : std::int32_t
{
	Unavailable = 0,
	Pending,
	Secure,
	Challenge
};

enum class InputOwner : std::int32_t
{
	None = 0,
	PauseMenu,
	Contract,
	Reward
};

struct RoomCandidate
{
	int room_index = -1;
	Role role = Role::Quiet;
	double distance_squared = 0.0;
	int initial_enemies = 0;
	bool is_start = false;
};

struct Selection
{
	int secure_room = -1;
	int challenge_room = -1;
	bool available = false;
};

struct Dispatch
{
	InputOwner owner = InputOwner::None;
	bool accepted = false;
};

Selection select_rooms(const std::vector<RoomCandidate>& rooms);
Dispatch dispatch_slot(bool pause_menu_active, bool contract_pending,
	bool reward_pending, int slot_index);
int challenge_damage(int committed_damage);
std::int64_t challenge_phase_ms(std::int64_t base_ms);
bool can_complete_floor(bool already_complete, int cleared_rooms, int required_rooms,
	Choice choice, bool selected_room_cleared);
}
