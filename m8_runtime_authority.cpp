// m8_runtime_authority.cpp - engine-independent policy implementation; see header.

#include "m8_runtime_authority.hpp"

namespace m8authority
{
namespace
{
bool can_enter_result(RunState state)
{
	return state == RunState::Playing || state == RunState::Paused;
}

bool can_restart(RunState state)
{
	return state == RunState::Paused
		|| state == RunState::Won
		|| state == RunState::Failed
		|| state == RunState::Error;
}
}

ExitDecision decide_exit(const ExitInput& input)
{
	ExitDecision out;
	out.objective_threshold_met = input.objective_threshold > 0
		&& input.objective_progress >= input.objective_threshold;
	out.withdrawal_satisfied = input.withdrawal == WithdrawalState::NotRequired
		|| input.withdrawal == WithdrawalState::Succeeded;
	out.objective_complete = out.objective_threshold_met && out.withdrawal_satisfied;
	// Once the objective threshold is visible, unresolved or failed optional-threat
	// withdrawal must remain visible as the reason progression is blocked.
	out.blocked_visible = out.objective_threshold_met && !out.withdrawal_satisfied;
	out.reward_available = out.objective_complete;
	out.descend_allowed = out.objective_complete && input.reward == RewardState::Resolved;
	return out;
}

LifecycleDecision apply_event(Lifecycle& lifecycle, RunEvent event)
{
	LifecycleDecision out;
	out.previous = lifecycle.state;

	switch (event)
	{
	case RunEvent::Tick:
		// Ticks are deliberately inert for lifecycle authority. Result/error states
		// require an explicit manual Restart event.
		break;
	case RunEvent::TogglePause:
		if (lifecycle.state == RunState::Playing)
		{
			lifecycle.state = RunState::Paused;
		}
		else if (lifecycle.state == RunState::Paused)
		{
			lifecycle.state = RunState::Playing;
		}
		break;
	case RunEvent::Win:
		if (can_enter_result(lifecycle.state))
		{
			lifecycle.state = RunState::Won;
		}
		break;
	case RunEvent::Fail:
		if (can_enter_result(lifecycle.state))
		{
			lifecycle.state = RunState::Failed;
		}
		break;
	case RunEvent::FinaleInitFail:
		if (can_enter_result(lifecycle.state))
		{
			lifecycle.state = RunState::Error;
		}
		break;
	case RunEvent::Restart:
		if (can_restart(lifecycle.state))
		{
			lifecycle.state = RunState::RestartPending;
			out.restart_requested = true;
		}
		break;
	case RunEvent::AckRestart:
		if (lifecycle.state == RunState::RestartPending)
		{
			lifecycle.state = RunState::Playing;
		}
		break;
	case RunEvent::Quit:
		if (lifecycle.state != RunState::QuitPending)
		{
			lifecycle.state = RunState::QuitPending;
			out.quit_requested = true;
		}
		break;
	}

	out.current = lifecycle.state;
	out.state_changed = out.previous != out.current;
	return out;
}
} // namespace m8authority
