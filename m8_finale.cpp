// m8_finale.cpp - see header.

#include "m8_finale.hpp"

#include <algorithm>
#include <limits>

namespace m8finale
{
namespace
{
	int percentage_of(int base, int pct)
	{
		if (base <= 0 || pct <= 0)
		{
			return 0;
		}
		const std::int64_t value =
			(static_cast<std::int64_t>(base) * pct + 50) / 100;
		return static_cast<int>(std::min<std::int64_t>(
			value, std::numeric_limits<int>::max()));
	}
}

int guard_for_resolve(int resolve_tokens)
{
	const int bounded = std::max(0, std::min(2, resolve_tokens));
	return std::max(kMinimumGuard, kBaseGuard - kGuardReductionPerResolve * bounded);
}

void configure(State& state, int resolve_tokens)
{
	state.configured = true;
	state.max_guard = guard_for_resolve(resolve_tokens);
	state.guard = state.max_guard;
	state.guard_broken_at_ms = -1;
}

Phase phase(const State& state, int current_hp, std::int64_t now_ms)
{
	if (!state.configured)
	{
		return Phase::Inactive;
	}
	if (current_hp <= 0)
	{
		return Phase::Dead;
	}
	if (state.guard > 0)
	{
		return Phase::Guarded;
	}
	if (state.guard_broken_at_ms >= 0
		&& now_ms <= state.guard_broken_at_ms + kStaggerMs)
	{
		return Phase::Staggered;
	}
	return Phase::Exposed;
}

int combat_pool_current(const State& state, int current_hp)
{
	return state.configured && state.guard > 0 ? state.guard : std::max(0, current_hp);
}

int combat_pool_max(const State& state, int max_hp)
{
	return state.configured && state.guard > 0 ? state.max_guard : std::max(1, max_hp);
}

DamageResult apply_damage(State& state, int current_hp, int amount, std::int64_t now_ms)
{
	DamageResult out;
	out.hp_after = std::max(0, current_hp);
	if (amount <= 0 || current_hp <= 0)
	{
		out.phase_after = phase(state, current_hp, now_ms);
		return out;
	}

	if (!state.configured)
	{
		out.hp_damage = std::min(amount, out.hp_after);
		out.hp_after -= out.hp_damage;
		out.phase_after = out.hp_after <= 0 ? Phase::Dead : Phase::Inactive;
		return out;
	}

	if (state.guard > 0)
	{
		out.guard_damage = std::min(amount, state.guard);
		state.guard -= out.guard_damage;
		if (state.guard == 0)
		{
			state.guard_broken_at_ms = now_ms;
			out.guard_broken = true;
		}
		// A guarded hit never spills into HP. This makes guard-break timing a deliberate
		// window instead of converting one oversized base swing into hidden HP damage.
		out.phase_after = phase(state, current_hp, now_ms);
		return out;
	}

	if (phase(state, current_hp, now_ms) == Phase::Staggered)
	{
		out.multiplier_pct = kStaggerDamagePct;
	}
	out.hp_damage = std::min(out.hp_after, percentage_of(amount, out.multiplier_pct));
	out.hp_after -= out.hp_damage;
	out.phase_after = phase(state, out.hp_after, now_ms);
	return out;
}

std::size_t select_warden(const std::vector<Candidate>& candidates, int farthest_room)
{
	std::size_t best = static_cast<std::size_t>(-1);
	for (std::size_t i = 0; i < candidates.size(); ++i)
	{
		const Candidate& c = candidates[i];
		if (!c.spawned || c.start_room || c.room_index != farthest_room)
		{
			continue;
		}
		if (best == static_cast<std::size_t>(-1)
			|| c.spawn_ordinal > candidates[best].spawn_ordinal)
		{
			best = i;
		}
	}
	if (best != static_cast<std::size_t>(-1))
	{
		return best;
	}

	for (std::size_t i = 0; i < candidates.size(); ++i)
	{
		const Candidate& c = candidates[i];
		if (!c.spawned || c.start_room)
		{
			continue;
		}
		if (best == static_cast<std::size_t>(-1)
			|| c.spawn_ordinal > candidates[best].spawn_ordinal)
		{
			best = i;
		}
	}
	return best;
}
}
