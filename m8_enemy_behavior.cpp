// m8_enemy_behavior.cpp - see header.

#include "m8_enemy_behavior.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace m8enemy
{
namespace
{
std::int64_t elapsed(const State& state, std::int64_t now_ms)
{
	return std::max<std::int64_t>(0, now_ms - state.phase_started_ms);
}

std::int64_t duration(const Input& input, std::int64_t base_ms)
{
	if (input.duration_numerator <= 0 || input.duration_denominator <= 0)
	{
		return base_ms;
	}
	const std::int64_t numerator = base_ms * static_cast<std::int64_t>(input.duration_numerator);
	return (numerator + input.duration_denominator / 2) / input.duration_denominator;
}

void transition(State& state, Phase phase, std::int64_t now_ms, Output& out)
{
	if (state.phase == phase)
	{
		return;
	}
	state.phase = phase;
	state.phase_started_ms = now_ms;
	out.phase_changed = true;
}

void commit_strike(State& state, const Input& input, bool in_range, Phase next, Output& out)
{
	++state.commit_serial;
	out.strike_committed = true;
	out.request_hit = in_range && input.has_los;
	out.attack_cancelled = !out.request_hit;
	out.commit_serial = state.commit_serial;
	transition(state, next, input.now_ms, out);
}

void finish_output(const State& state, Output& out)
{
	out.phase = state.phase;
	out.commit_serial = state.commit_serial;
	out.dash_target = state.dash_target;
	out.circle_direction = state.circle_direction;
}
}

Archetype clamp_archetype(int type_id)
{
	return type_id == static_cast<int>(Archetype::Runner) ? Archetype::Runner
		: (type_id == static_cast<int>(Archetype::Brute) ? Archetype::Brute : Archetype::Grunt);
}

int scale_contact_damage(int base_damage, int percent)
{
	if (base_damage <= 0 || percent <= 0)
	{
		return 0;
	}
	const std::int64_t numerator =
		static_cast<std::int64_t>(base_damage) * static_cast<std::int64_t>(percent) + 50;
	const std::int64_t rounded = numerator / 100;
	return static_cast<int>(std::clamp<std::int64_t>(
		rounded, 1, std::numeric_limits<int>::max()));
}

int runner_circle_direction(std::uint32_t spawn_ordinal)
{
	return (spawn_ordinal % 2u == 0u) ? 1 : -1;
}

Vec2 cap_dash_target(Vec2 self, Vec2 target)
{
	const double dx = target.x - self.x;
	const double dy = target.y - self.y;
	const double distance = std::sqrt(dx * dx + dy * dy);
	if (distance <= kRunnerDashCap || distance <= 0.0)
	{
		return target;
	}
	const double scale = kRunnerDashCap / distance;
	return { self.x + dx * scale, self.y + dy * scale };
}

void initialize(State& state, Archetype archetype, std::uint32_t spawn_ordinal, std::int64_t now_ms)
{
	state.archetype = archetype;
	state.spawn_ordinal = spawn_ordinal;
	state.circle_direction = runner_circle_direction(spawn_ordinal);
	reset(state, now_ms);
}

void reset(State& state, std::int64_t now_ms, bool dead)
{
	state.phase = dead ? Phase::Dead : Phase::Dormant;
	state.phase_started_ms = now_ms;
	state.dash_target = {};
	state.dash_target_valid = false;
	state.commit_serial = 0;
}

Output update(State& state, const Input& input)
{
	Output out;
	out.previous_phase = state.phase;

	if (!input.alive)
	{
		if (state.phase != Phase::Dead || state.dash_target_valid || state.commit_serial != 0)
		{
			reset(state, input.now_ms, true);
			out.reset = true;
			out.phase_changed = out.previous_phase != state.phase;
		}
		finish_output(state, out);
		return out;
	}

	if (!input.active || !input.within_leash)
	{
		if (state.phase != Phase::Dormant || state.dash_target_valid || state.commit_serial != 0)
		{
			reset(state, input.now_ms);
			out.reset = true;
			out.phase_changed = out.previous_phase != state.phase;
		}
		finish_output(state, out);
		return out;
	}

	if (state.phase == Phase::Dead)
	{
		reset(state, input.now_ms);
		out.reset = true;
		out.phase_changed = true;
	}

	switch (state.phase)
	{
	case Phase::Dormant:
		transition(state,
			state.archetype == Archetype::Runner ? Phase::RunnerCircle : Phase::Approach,
			input.now_ms, out);
		break;
	case Phase::Approach:
		if (input.has_los && state.archetype == Archetype::Grunt && input.in_attack_range)
		{
			transition(state, Phase::GruntWindup, input.now_ms, out);
		}
		else if (input.has_los && state.archetype == Archetype::Brute && input.in_danger_radius)
		{
			transition(state, Phase::BruteTell, input.now_ms, out);
		}
		break;
	case Phase::GruntWindup:
		if (elapsed(state, input.now_ms) >= duration(input, kGruntWindupMs))
		{
			commit_strike(state, input, input.in_attack_range, Phase::Recovery, out);
		}
		break;
	case Phase::RunnerCircle:
		if (elapsed(state, input.now_ms) >= duration(input, kRunnerCircleMs))
		{
			transition(state, Phase::RunnerTell, input.now_ms, out);
		}
		break;
	case Phase::RunnerTell:
		if (elapsed(state, input.now_ms) >= duration(input, kRunnerTellMs))
		{
			if (!input.has_los || !input.nav_valid)
			{
				state.dash_target_valid = false;
				out.attack_cancelled = true;
				transition(state, Phase::RunnerDisengage, input.now_ms, out);
			}
			else
			{
				state.dash_target = cap_dash_target(input.self, input.target);
				state.dash_target_valid = true;
				out.dash_started = true;
				transition(state, Phase::RunnerDash, input.now_ms, out);
			}
		}
		break;
	case Phase::RunnerDash:
		if (!input.has_los || !input.nav_valid)
		{
			state.dash_target_valid = false;
			out.attack_cancelled = true;
			transition(state, Phase::RunnerDisengage, input.now_ms, out);
		}
		else if (input.dash_complete || elapsed(state, input.now_ms) >= duration(input, kRunnerDashMaxMs))
		{
			state.dash_target_valid = false;
			commit_strike(state, input, input.in_attack_range, Phase::RunnerDisengage, out);
		}
		break;
	case Phase::RunnerDisengage:
		if (elapsed(state, input.now_ms) >= duration(input, kRunnerDisengageMs))
		{
			transition(state, Phase::RunnerCircle, input.now_ms, out);
		}
		break;
	case Phase::BruteTell:
		if (elapsed(state, input.now_ms) >= duration(input, kBruteTellMs))
		{
			commit_strike(state, input, input.in_danger_radius, Phase::Recovery, out);
		}
		break;
	case Phase::Recovery:
	{
		const std::int64_t recovery =
			state.archetype == Archetype::Brute ? kBruteRecoveryMs : kGruntRecoveryMs;
		if (elapsed(state, input.now_ms) >= duration(input, recovery))
		{
			transition(state, Phase::Approach, input.now_ms, out);
		}
		break;
	}
	case Phase::Dead:
	default:
		break;
	}

	finish_output(state, out);
	return out;
}

const char* phase_name(Phase phase)
{
	switch (phase)
	{
	case Phase::Dormant: return "Dormant";
	case Phase::Approach: return "Approach";
	case Phase::GruntWindup: return "GruntWindup";
	case Phase::RunnerCircle: return "RunnerCircle";
	case Phase::RunnerTell: return "RunnerTell";
	case Phase::RunnerDash: return "RunnerDash";
	case Phase::RunnerDisengage: return "RunnerDisengage";
	case Phase::BruteTell: return "BruteTell";
	case Phase::Recovery: return "Recovery";
	case Phase::Dead: return "Dead";
	default: return "?";
	}
}
}
