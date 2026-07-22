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
	require(guard_for_resolve(0) == 120, "zero Resolve keeps full guard");
	require(guard_for_resolve(1) == 90, "one Resolve removes 30 guard");
	require(guard_for_resolve(2) == 60, "two Resolve reaches minimum guard");
	require(guard_for_resolve(99) == 60, "Resolve is bounded");

	State state;
	configure(state, 1);
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
	require(select_warden(fallback, 4) == 2,
		"empty farthest room falls back to highest non-start ordinal");
	require(select_warden({ { 0, 1, true, true } }, 4) == static_cast<std::size_t>(-1),
		"no non-start successful spawn fails closed");

	std::cout << "m8_finale: PASS\n";
	return 0;
}
