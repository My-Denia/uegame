// m8_runtime_authority.hpp - deterministic engine-independent exit and run policy.
//
// This core decides policy only. It does not integrate with or prove behavior in UE,
// PIE, or Shipping; an adapter must translate its one-shot intents to runtime work.

#pragma once

namespace m8authority
{
enum class WithdrawalState
{
	NotRequired = 0,
	Pending,
	Succeeded,
	Failed
};

enum class RewardState
{
	Unavailable = 0,
	Pending,
	Resolved
};

struct ExitInput
{
	int objective_progress = 0;
	int objective_threshold = 1;
	WithdrawalState withdrawal = WithdrawalState::NotRequired;
	RewardState reward = RewardState::Unavailable;
};

struct ExitDecision
{
	bool objective_threshold_met = false;
	bool withdrawal_satisfied = false;
	bool objective_complete = false;
	bool blocked_visible = false;
	bool reward_available = false;
	bool descend_allowed = false;
};

ExitDecision decide_exit(const ExitInput& input);

enum class RunState
{
	Playing = 0,
	Paused,
	Won,
	Failed,
	Error,
	RestartPending,
	QuitPending
};

enum class RunEvent
{
	Tick = 0,
	TogglePause,
	Win,
	Fail,
	FinaleInitFail,
	Restart,
	AckRestart,
	Quit
};

struct Lifecycle
{
	RunState state = RunState::Playing;
};

struct LifecycleDecision
{
	RunState previous = RunState::Playing;
	RunState current = RunState::Playing;
	bool state_changed = false;
	bool restart_requested = false;
	bool quit_requested = false;
};

// restart_requested and quit_requested are one-shot intents emitted only when the
// corresponding pending state is first entered.
LifecycleDecision apply_event(Lifecycle& lifecycle, RunEvent event);
} // namespace m8authority
