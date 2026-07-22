#include "m8_room_flow.hpp"
#include "m8_room_contract.hpp"

#include <cmath>
#include <iostream>
#include <limits>

namespace
{
int failures = 0;
void expect(bool condition, const char* label)
{
	if (!condition) { std::cerr << "FAIL: " << label << '\n'; ++failures; }
}
bool near(double a, double b) { return std::abs(a - b) < 1e-9; }
}

int main()
{
	const m8room::Config config;
	expect(m8room::validate(config), "default config valid");
	expect(m8room::is_first_true_clear(1, 0), "positive to zero is first true clear");
	expect(!m8room::is_first_true_clear(0, 0), "zero to zero is not another clear");
	expect(m8room::required_combat_rooms(1, 3, 8, config) == 5, "floor 1 quota is five");
	expect(m8room::required_combat_rooms(2, 3, 8, config) == 6, "floor 2 quota is six");
	expect(m8room::required_combat_rooms(3, 3, 8, config) == 8, "final floor requires full clear");
	expect(m8room::required_combat_rooms(1, 3, 0, config) == 0, "zero actual rooms is safe");
	m8room::Config tuned = config;
	tuned.required_combat_rooms_base = 4;
	tuned.required_combat_rooms_per_floor = 1;
	expect(m8room::required_combat_rooms(1, 3, 8, tuned) == 4, "tuned floor 1 quota is four");
	expect(m8room::required_combat_rooms(2, 3, 8, tuned) == 5, "tuned floor 2 quota is five");
	expect(m8room::required_combat_rooms(3, 3, 8, tuned) == 8, "tuned final floor still requires full clear");
	expect(m8room::should_transition_to_complete(false, 5, 5), "quota crossing transitions once");
	expect(!m8room::should_transition_to_complete(true, 6, 5), "completed objective never transitions twice");
	expect(m8room::should_neutralize_optional_threats(true, 1, 3, 4, 8),
		"first non-final completion abandons optional threats");
	expect(!m8room::should_neutralize_optional_threats(false, 1, 3, 4, 8),
		"repeat completion never neutralizes twice");
	expect(!m8room::should_neutralize_optional_threats(true, 3, 3, 8, 8),
		"final floor never neutralizes threats");
	expect(!m8room::should_neutralize_optional_threats(true, 1, 3, 8, 8),
		"full clear has no optional threats");
	expect(!m8room::should_neutralize_optional_threats(true, 0, 3, 0, 8),
		"invalid floor cannot neutralize threats");
	expect(m8room::required_combat_rooms(
		std::numeric_limits<int>::max() - 1, std::numeric_limits<int>::max(), 9, config) == 9,
		"large floor arithmetic saturates without overflow");

	const auto quiet = m8room::apply_room_clear(m8room::Role::Quiet, 50.0, 100.0, config);
	const auto standard = m8room::apply_room_clear(m8room::Role::Standard, 50.0, 100.0, config);
	const auto skirmish = m8room::apply_room_clear(m8room::Role::Skirmish, 50.0, 100.0, config);
	const auto stronghold = m8room::apply_room_clear(m8room::Role::Stronghold, 95.0, 100.0, config);
	expect(near(quiet.after, 50.0), "quiet grants zero");
	expect(near(standard.after, 53.0), "standard grants 3 percent max");
	expect(near(skirmish.after, 56.0), "skirmish grants 6 percent max");
	expect(near(stronghold.after, 100.0) && near(stronghold.restored, 5.0), "stronghold clamps at max");

	const auto reward = m8room::apply_reward_floor(9.0, 140.0, config);
	expect(near(reward.after, 84.0) && near(reward.restored, 75.0), "9 of 140 recovers to 84");
	expect(near(m8room::apply_reward_floor(140.0, 140.0, config).after, 140.0), "full health unchanged");
	expect(near(m8room::apply_reward_floor(0.0, 140.0, config).after, 0.0), "zero health never resurrects");
	expect(near(m8room::apply_reward_floor(-20.0, 140.0, config).after, 0.0), "negative input never yields negative");
	expect(near(m8room::apply_reward_floor(200.0, 140.0, config).after, 140.0), "over-max input clamps to max");
	tuned.room_clear_fraction = {{0.0, 0.03, 0.06, 0.10}};
	tuned.reward_floor_fraction = 1.0;
	expect(near(m8room::apply_room_clear(m8room::Role::Standard, 50.0, 100.0, tuned).after, 53.0),
		"tuned standard recovery grants 3 percent max");
	expect(near(m8room::apply_room_clear(m8room::Role::Skirmish, 50.0, 100.0, tuned).after, 56.0),
		"tuned skirmish recovery grants 6 percent max");
	expect(near(m8room::apply_room_clear(m8room::Role::Stronghold, 50.0, 100.0, tuned).after, 60.0),
		"tuned stronghold recovery grants 10 percent max");
	expect(near(m8room::apply_reward_floor(9.0, 140.0, tuned).after, 140.0),
		"tuned reward recovery restores living player to full");

	m8room::Config invalid = config;
	invalid.room_clear_fraction[1] = -0.01;
	expect(!m8room::validate(invalid), "negative fraction rejected");
	invalid = config;
	invalid.reward_floor_fraction = 1.01;
	expect(!m8room::validate(invalid), "over-one fraction rejected");
	invalid = config;
	invalid.reward_floor_fraction = std::numeric_limits<double>::quiet_NaN();
	expect(!m8room::validate(invalid), "non-finite fraction rejected");
	invalid = config;
	invalid.required_combat_rooms_base = -1;
	expect(!m8room::validate(invalid), "negative base quota rejected");
	invalid = config;
	invalid.required_combat_rooms_per_floor = 1025;
	expect(!m8room::validate(invalid), "unbounded quota step rejected");

	using m8contract::Choice;
	using m8contract::InputOwner;
	using m8contract::Role;
	std::vector<m8contract::RoomCandidate> rooms = {
		{0, Role::Quiet, 0.0, 0, true},
		{11, Role::Standard, 100.0, 4, false},
		{2, Role::Stronghold, 900.0, 6, false},
		{3, Role::Standard, 100.0, 4, false}
	};
	const auto selected = m8contract::select_rooms(rooms);
	expect(selected.available && selected.secure_room == 3,
		"secure uses minimum distance and lower room tie");
	expect(selected.challenge_room == 2, "challenge prefers farthest stronghold");
	rooms = {{4, Role::Standard, 25.0, 3, false}, {7, Role::Standard, 100.0, 3, false}};
	const auto fallback = m8contract::select_rooms(rooms);
	expect(fallback.secure_room == 4 && fallback.challenge_room == 7,
		"challenge fallback uses farthest eligible room");
	expect(!m8contract::select_rooms({{0, Role::Quiet, 0.0, 0, true}}).available,
		"zero candidates becomes unavailable");
	expect(m8contract::dispatch_slot(true, true, true, 0).owner == InputOwner::PauseMenu,
		"pause menu owns input first");
	const auto invalid_contract_key = m8contract::dispatch_slot(false, true, true, 2);
	expect(invalid_contract_key.owner == InputOwner::Contract && !invalid_contract_key.accepted,
		"pending contract consumes invalid slot without leaking to reward");
	const auto contract_key = m8contract::dispatch_slot(false, true, true, 1);
	expect(contract_key.owner == InputOwner::Contract && contract_key.accepted,
		"pending contract accepts challenge slot");
	expect(m8contract::dispatch_slot(false, false, true, 2).owner == InputOwner::Reward,
		"reward owns slot after contract resolves");
	expect(m8contract::challenge_damage(5) == 6 && m8contract::challenge_damage(4) == 5
		&& m8contract::challenge_damage(8) == 10,
		"challenge applies after committed 5 4 8 damage");
	expect(m8contract::challenge_phase_ms(450) == 375
		&& m8contract::challenge_phase_ms(350) == 292
		&& m8contract::challenge_phase_ms(1200) == 1000,
		"challenge phase timings use exact five sixths half up");
	expect(!m8contract::can_complete_floor(false, 5, 5, Choice::Challenge, false),
		"quota cannot bypass unresolved selected contract room");
	expect(m8contract::can_complete_floor(false, 5, 5, Choice::Challenge, true),
		"resolved contract permits quota completion");
	expect(m8contract::can_complete_floor(false, 5, 5, Choice::Unavailable, false),
		"unavailable contract cannot softlock quota");
	expect(!m8contract::can_complete_floor(true, 6, 5, Choice::Unavailable, false),
		"completed contract flow remains idempotent");

	if (failures == 0) { std::cout << "m8_room_flow PASS\n"; }
	return failures == 0 ? 0 : 1;
}
