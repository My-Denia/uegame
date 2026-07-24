// test_m8_runtime_authority.cpp - engine-independent policy coverage only.
// Passing this executable does not prove UE, PIE, or Shipping integration.

#include "m8_runtime_authority.hpp"

#include <iostream>

namespace
{
int failures = 0;

void check(bool condition, const char* label)
{
	if (!condition)
	{
		++failures;
		std::cerr << "FAIL: " << label << '\n';
	}
}
}

int main()
{
	using namespace m8authority;

	ExitInput exit;
	exit.objective_progress = 4;
	exit.objective_threshold = 5;
	exit.withdrawal = WithdrawalState::Succeeded;
	exit.reward = RewardState::Resolved;
	ExitDecision decision = decide_exit(exit);
	check(!decision.objective_threshold_met && !decision.objective_complete,
		"below-threshold objective remains incomplete");
	check(!decision.reward_available && !decision.descend_allowed,
		"withdrawal and reward cannot bypass objective threshold");

	exit.objective_progress = 5;
	exit.withdrawal = WithdrawalState::Pending;
	exit.reward = RewardState::Resolved;
	decision = decide_exit(exit);
	check(decision.objective_threshold_met && !decision.withdrawal_satisfied,
		"pending optional-threat withdrawal is unsatisfied");
	check(!decision.objective_complete && decision.blocked_visible,
		"pending withdrawal keeps objective visibly blocked");
	check(!decision.reward_available && !decision.descend_allowed,
		"pending withdrawal blocks reward and descend");

	exit.withdrawal = WithdrawalState::Failed;
	decision = decide_exit(exit);
	check(!decision.objective_complete && decision.blocked_visible,
		"failed withdrawal is visibly blocked");
	check(!decision.reward_available && !decision.descend_allowed,
		"failed withdrawal cannot reward or descend");

	exit.withdrawal = WithdrawalState::Succeeded;
	exit.reward = RewardState::Pending;
	decision = decide_exit(exit);
	check(decision.objective_complete && decision.reward_available,
		"threshold plus successful withdrawal enables objective reward");
	check(!decision.blocked_visible && !decision.descend_allowed,
		"pending reward blocks descend after objective completion");

	exit.reward = RewardState::Resolved;
	decision = decide_exit(exit);
	check(decision.objective_complete && decision.reward_available
		&& decision.descend_allowed,
		"resolved reward permits descend");

	exit.withdrawal = WithdrawalState::NotRequired;
	exit.reward = RewardState::Resolved;
	decision = decide_exit(exit);
	check(decision.withdrawal_satisfied && decision.objective_complete
		&& decision.descend_allowed,
		"no optional threats needs no withdrawal work");

	exit.objective_threshold = 0;
	decision = decide_exit(exit);
	check(!decision.objective_threshold_met && !decision.objective_complete
		&& !decision.reward_available && !decision.descend_allowed,
		"invalid threshold fails closed");

	Lifecycle lifecycle;
	LifecycleDecision transition = apply_event(lifecycle, RunEvent::Tick);
	check(lifecycle.state == RunState::Playing && !transition.state_changed,
		"playing tick is inert");
	transition = apply_event(lifecycle, RunEvent::Restart);
	check(lifecycle.state == RunState::Playing && !transition.restart_requested,
		"playing cannot manually restart");
	transition = apply_event(lifecycle, RunEvent::AckRestart);
	check(lifecycle.state == RunState::Playing && !transition.state_changed,
		"restart acknowledgement without request is inert");

	transition = apply_event(lifecycle, RunEvent::TogglePause);
	check(lifecycle.state == RunState::Paused && transition.state_changed,
		"playing toggles to paused");
	transition = apply_event(lifecycle, RunEvent::TogglePause);
	check(lifecycle.state == RunState::Playing && transition.state_changed,
		"paused toggles to playing");

	for (const RunEvent result_event :
		{ RunEvent::Win, RunEvent::Fail, RunEvent::FinaleInitFail })
	{
		Lifecycle result_lifecycle;
		transition = apply_event(result_lifecycle, result_event);
		const RunState expected = result_event == RunEvent::Win ? RunState::Won
			: (result_event == RunEvent::Fail ? RunState::Failed : RunState::Error);
		check(result_lifecycle.state == expected && transition.state_changed,
			"result event enters explicit terminal state");
		transition = apply_event(result_lifecycle, result_event);
		check(result_lifecycle.state == expected && !transition.state_changed,
			"repeated result event is idempotent");
		for (int tick = 0; tick < 4; ++tick)
		{
			transition = apply_event(result_lifecycle, RunEvent::Tick);
			check(result_lifecycle.state == expected && !transition.state_changed
				&& !transition.restart_requested,
				"terminal ticks never auto-restart");
		}
		transition = apply_event(result_lifecycle, RunEvent::TogglePause);
		check(result_lifecycle.state == expected && !transition.state_changed,
			"pause toggle is ignored outside playing or paused");
		transition = apply_event(result_lifecycle, RunEvent::Restart);
		check(result_lifecycle.state == RunState::RestartPending
			&& transition.restart_requested && transition.state_changed,
			"manual restart emits one request from result or error");
		transition = apply_event(result_lifecycle, RunEvent::Restart);
		check(result_lifecycle.state == RunState::RestartPending
			&& !transition.restart_requested && !transition.state_changed,
			"pending restart cannot emit twice");
		transition = apply_event(result_lifecycle, RunEvent::AckRestart);
		check(result_lifecycle.state == RunState::Playing && transition.state_changed,
			"restart acknowledgement returns to playing");
	}

	Lifecycle paused_restart;
	apply_event(paused_restart, RunEvent::TogglePause);
	transition = apply_event(paused_restart, RunEvent::Restart);
	check(paused_restart.state == RunState::RestartPending && transition.restart_requested,
		"paused state permits one manual restart request");
	transition = apply_event(paused_restart, RunEvent::Restart);
	check(!transition.restart_requested && !transition.state_changed,
		"paused restart remains idempotent while pending");
	apply_event(paused_restart, RunEvent::AckRestart);
	check(paused_restart.state == RunState::Playing,
		"paused restart acknowledgement returns to playing");

	Lifecycle quit;
	transition = apply_event(quit, RunEvent::Quit);
	check(quit.state == RunState::QuitPending && transition.quit_requested
		&& transition.state_changed,
		"quit emits one request");
	for (int attempt = 0; attempt < 3; ++attempt)
	{
		transition = apply_event(quit, RunEvent::Quit);
		check(quit.state == RunState::QuitPending && !transition.quit_requested
			&& !transition.state_changed,
			"quit request cannot emit twice");
	}
	transition = apply_event(quit, RunEvent::AckRestart);
	check(quit.state == RunState::QuitPending && !transition.state_changed,
		"restart acknowledgement cannot escape quit pending");

	std::cout << "m8_runtime_authority: "
		<< (failures == 0 ? "PASS" : "FAIL")
		<< " (engine-independent policy only; does not prove UE/PIE/Shipping)"
		<< '\n';
	return failures == 0 ? 0 : 1;
}
