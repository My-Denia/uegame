#include "m8_finale.hpp"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
void require(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
		std::exit(1);
	}
}
}

int main()
{
	using namespace m8finale;
	const Profile profile{120, 30, 60, 2500, 150};
	require(validate_profile(profile), "fixed profile validates");
	require(!validate_profile({120, 30, 70, 2500, 150}), "inconsistent guard profile rejected");
	require(!validate_profile({120, 30, 60, 0, 150}), "zero stagger rejected");
	require(!validate_profile({120, 30, 60, 2500, 99}), "sub-unity multiplier rejected");
	require(guard_for_resolve(0, profile) == 120, "zero Resolve keeps full guard");
	require(guard_for_resolve(1, profile) == 90, "one Resolve removes 30 guard");
	require(guard_for_resolve(2, profile) == 60, "two Resolve reaches minimum guard");
	require(guard_for_resolve(99, profile) == 60, "Resolve is bounded");

	State state;
	require(configure(state, 1, profile), "valid profile configures");
	require(phase(state, 200, 0) == Phase::Guarded, "configured Warden starts guarded");
	require(combat_pool_current(state, 200) == 90 && combat_pool_max(state, 200) == 90,
		"Executioner reads guard ratio while guarded");

	DamageResult hit = apply_damage(state, 200, 80, 1000);
	require(hit.guard_damage == 80 && hit.hp_damage == 0 && state.guard == 10,
		"guard substitutes for HP damage");
	hit = apply_damage(state, 200, 30, 1200);
	require(hit.guard_broken && hit.guard_damage == 10 && hit.hp_damage == 0,
		"guard break discards overflow and opens a deliberate window");
	require(phase(state, 200, 3700) == Phase::Staggered,
		"stagger end-point is inclusive");
	hit = apply_damage(state, 200, 20, 3700);
	require(hit.multiplier_pct == 150 && hit.hp_damage == 30 && hit.hp_after == 170,
		"stagger uses half-up 150 percent HP damage");
	hit = apply_damage(state, 170, 20, 3701);
	require(hit.multiplier_pct == 100 && hit.hp_damage == 20 && hit.phase_after == Phase::Exposed,
		"post-stagger damage is exposed normal damage");
	hit = apply_damage(state, 20, 50, 4000);
	require(hit.hp_after == 0 && hit.phase_after == Phase::Dead,
		"HP death terminates finale state");

	State ordinary;
	hit = apply_damage(ordinary, 30, 12, 0);
	require(hit.hp_after == 18 && hit.phase_after == Phase::Inactive,
		"unconfigured enemies preserve ordinary damage");

	const std::vector<Candidate> preferred = {
		{ 0, 9, true, true }, { 4, 3, true, false }, { 4, 7, true, false },
		{ 2, 11, true, false }, { 4, 12, false, false }
	};
	require(select_warden(preferred, 4) == 2,
		"highest successful ordinal in farthest room wins");
	const std::vector<Candidate> fallback = {
		{ 0, 50, true, true }, { 2, 4, true, false }, { 3, 8, true, false }
	};
	require(!validate_candidates(fallback, 3, 5),
		"start-room candidate is rejected from the atomic candidate set");
	require(select_warden(fallback, 4) == 2,
		"empty farthest room falls back to highest non-start ordinal");
	const std::vector<Candidate> valid = {
		{ 2, 4, true, false }, { 3, 8, true, false }, { 4, 9, true, false }
	};
	require(validate_candidates(valid, 3, 6), "complete unique candidate set validates");
	const std::vector<Candidate> duplicate = {
		{ 2, 4, true, false }, { 3, 4, true, false }
	};
	require(!validate_candidates(duplicate, 2, 6), "duplicate ordinal rejected");
	require(select_warden({ { 0, 1, true, true } }, 4) == static_cast<std::size_t>(-1),
		"no non-start successful spawn fails closed");
	reset(state);
	require(!state.configured && state.guard == 0 && phase(state, 300, 0) == Phase::Inactive,
		"reset clears identity guard and phase");

	std::cout << "m8_finale: PASS\n";
	return 0;
}
