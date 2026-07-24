// m8_finale.hpp - RNG-free additive Warden rules for the portfolio finale.
//
// The core owns only guard/phase arithmetic and deterministic selection. Enemy HP,
// actor lifetime, movement, spawning, and presentation remain authoritative in UE.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace m8finale
{
struct Profile
{
	int base_guard = 120;
	int guard_reduction_per_resolve = 30;
	int minimum_guard = 60;
	std::int64_t stagger_ms = 2500;
	int stagger_damage_pct = 150;
};

enum class Phase
{
	Inactive = 0,
	Guarded,
	Staggered,
	Exposed,
	Dead
};

struct State
{
	bool configured = false;
	int max_guard = 0;
	int guard = 0;
	std::int64_t guard_broken_at_ms = -1;
	std::int64_t stagger_ms = 0;
	int stagger_damage_pct = 100;
};

struct Candidate
{
	int room_index = -1;
	std::uint32_t spawn_ordinal = 0;
	bool spawned = false;
	bool start_room = false;
};

struct DamageResult
{
	int guard_damage = 0;
	int hp_damage = 0;
	int hp_after = 0;
	int multiplier_pct = 100;
	bool guard_broken = false;
	Phase phase_after = Phase::Inactive;
};

bool validate_profile(const Profile& profile);
int guard_for_resolve(int resolve_tokens, const Profile& profile);
bool configure(State& state, int resolve_tokens, const Profile& profile);
void reset(State& state);
Phase phase(const State& state, int current_hp, std::int64_t now_ms);
int combat_pool_current(const State& state, int current_hp);
int combat_pool_max(const State& state, int max_hp);
DamageResult apply_damage(State& state, int current_hp, int amount, std::int64_t now_ms);

// Preferred selection: highest ordinal in the generated farthest non-start room.
// If that room received no successful spawn, fall back to the highest ordinal
// successfully spawned in any non-start room. Returns size_t(-1) on failure.
std::size_t select_warden(const std::vector<Candidate>& candidates, int farthest_room);
bool validate_candidates(
	const std::vector<Candidate>& candidates, std::size_t expected_count, int room_count);
}
