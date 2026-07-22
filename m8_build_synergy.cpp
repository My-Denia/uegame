// m8_build_synergy.cpp - see header.

#include "m8_build_synergy.hpp"

#include <algorithm>
#include <limits>

namespace m8
{
namespace
{
	bool any_rank(const Ranks& ranks)
	{
		return ranks.executioner > 0 || ranks.tempo > 0 || ranks.bulwark > 0;
	}

	int apply_to_hp(int hp, int damage)
	{
		return std::max(0, hp - std::max(0, damage));
	}
}

int clamp_rank(int rank)
{
	return std::max(0, std::min(2, rank));
}

int percentage_of(int base, int pct)
{
	if (base <= 0 || pct <= 0)
	{
		return 0;
	}
	const std::int64_t value =
		(static_cast<std::int64_t>(base) * static_cast<std::int64_t>(pct) + 50) / 100;
	return static_cast<int>(std::min<std::int64_t>(value, std::numeric_limits<int>::max()));
}

bool at_or_below_threshold(int current_hp, int max_hp, int threshold_pct)
{
	if (max_hp <= 0 || threshold_pct < 0)
	{
		return false;
	}
	return 100LL * static_cast<std::int64_t>(std::max(0, current_hp))
		<= static_cast<std::int64_t>(threshold_pct) * static_cast<std::int64_t>(max_hp);
}

std::size_t select_primary(const std::vector<TargetSnapshot>& candidates)
{
	if (candidates.empty())
	{
		return static_cast<std::size_t>(-1);
	}
	std::size_t best = 0;
	for (std::size_t i = 1; i < candidates.size(); ++i)
	{
		const TargetSnapshot& a = candidates[i];
		const TargetSnapshot& b = candidates[best];
		const std::int64_t a_max = std::max(1, a.max_hp);
		const std::int64_t b_max = std::max(1, b.max_hp);
		const std::int64_t left = static_cast<std::int64_t>(std::max(0, a.current_hp)) * b_max;
		const std::int64_t right = static_cast<std::int64_t>(std::max(0, b.current_hp)) * a_max;
		if (left < right
			|| (left == right && (a.distance_squared < b.distance_squared
				|| (a.distance_squared == b.distance_squared && a.spawn_ordinal < b.spawn_ordinal))))
		{
			best = i;
		}
	}
	return best;
}

void reset_transient(State& state)
{
	state.tempo_chain = 0;
	state.counter_active = false;
	state.counter_expires_at_ms = 0;
}

bool sync_loadout(State& state, const Ranks& ranks, std::uint64_t fingerprint)
{
	const bool empty = !any_rank(ranks) || fingerprint == 0;
	if (empty || state.loadout_fingerprint != fingerprint)
	{
		reset_transient(state);
		state.loadout_fingerprint = empty ? 0 : fingerprint;
		return true;
	}
	return false;
}

DamageEventResult on_owner_damage(
	State& state, const Ranks& ranks, int amount, bool enemy_instigated, std::int64_t now_ms)
{
	DamageEventResult out;
	if (amount <= 0)
	{
		return out;
	}

	out.tempo_reset = state.tempo_chain != 0;
	state.tempo_chain = 0;

	if (enemy_instigated && clamp_rank(ranks.bulwark) > 0)
	{
		const int window = clamp_rank(ranks.bulwark) >= 2
			? kBulwarkRank2WindowMs : kBulwarkRank1WindowMs;
		state.counter_active = true;
		state.counter_expires_at_ms = now_ms + window;
		out.counter_opened = true;
		out.counter_expires_at_ms = state.counter_expires_at_ms;
	}
	return out;
}

std::int64_t counter_remaining_ms(const State& state, std::int64_t now_ms)
{
	if (!state.counter_active || now_ms > state.counter_expires_at_ms)
	{
		return 0;
	}
	return state.counter_expires_at_ms - now_ms;
}

SwingResult resolve_accepted_swing(State& state, const SwingInput& input)
{
	SwingResult out;
	if (!input.accepted)
	{
		out.tempo_chain_after = state.tempo_chain;
		out.primary_hp_after_synergies = input.has_primary
			? std::max(0, input.primary_hp_after_base) : 0;
		return out;
	}
	const int executioner_rank = clamp_rank(input.ranks.executioner);
	const int tempo_rank = clamp_rank(input.ranks.tempo);
	const int bulwark_rank = clamp_rank(input.ranks.bulwark);
	const bool successful = input.hits > 0;
	int hp = input.has_primary ? std::max(0, input.primary_hp_after_base) : 0;

	// Executioner uses the pre-base snapshot and never retargets after base damage.
	out.executioner_qualified = input.has_primary && executioner_rank > 0
		&& at_or_below_threshold(
			input.primary_current_hp, input.primary_max_hp, kExecutionerThresholdPct);
	if (out.executioner_qualified && hp > 0)
	{
		out.executioner_damage = percentage_of(input.base_damage, kExecutionerBonusPct);
		out.executioner_applied = out.executioner_damage > 0;
		hp = apply_to_hp(hp, out.executioner_damage);
	}

	// Tempo advances once per successful swing, not once per target.
	if (tempo_rank > 0)
	{
		if (!successful)
		{
			out.tempo_reset_by_miss = state.tempo_chain != 0;
			state.tempo_chain = 0;
		}
		else
		{
			const int threshold = tempo_rank >= 2 ? 2 : 3;
			++state.tempo_chain;
			if (state.tempo_chain >= threshold)
			{
				state.tempo_chain = 0;
				out.tempo_proc = true;
				if (input.has_primary && hp > 0)
				{
					out.tempo_damage = percentage_of(input.base_damage, kTempoBonusPct);
					out.tempo_applied = out.tempo_damage > 0;
					hp = apply_to_hp(hp, out.tempo_damage);
				}
			}
		}
	}
	else
	{
		state.tempo_chain = 0;
	}

	// Bulwark expiry is strict greater-than; equality remains valid. Only success consumes.
	if (state.counter_active && input.now_ms > state.counter_expires_at_ms)
	{
		state.counter_active = false;
		state.counter_expires_at_ms = 0;
	}
	if (bulwark_rank > 0 && successful && state.counter_active)
	{
		state.counter_active = false;
		state.counter_expires_at_ms = 0;
		out.counter_consumed = true;
		if (input.has_primary && hp > 0)
		{
			out.bulwark_damage = percentage_of(input.base_damage, kBulwarkBonusPct);
			out.bulwark_applied = out.bulwark_damage > 0;
			hp = apply_to_hp(hp, out.bulwark_damage);
		}
		if (bulwark_rank >= 2)
		{
			out.heal = kBulwarkRank2Heal;
		}
	}
	else if (bulwark_rank == 0)
	{
		state.counter_active = false;
		state.counter_expires_at_ms = 0;
	}

	// Executioner rank-2 observes death at the end of the full E -> T -> B transaction.
	if (executioner_rank >= 2 && out.executioner_qualified && input.has_primary && hp <= 0)
	{
		out.cooldown_refund_ms = std::min(
			std::max(0, input.attack_cooldown_ms),
			percentage_of(std::max(0, input.attack_cooldown_ms), kExecutionerRefundPct));
	}

	out.primary_hp_after_synergies = hp;
	out.tempo_chain_after = state.tempo_chain;
	return out;
}
}
