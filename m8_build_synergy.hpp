// m8_build_synergy.hpp - RNG-free, engine-agnostic M8A build-synergy rules/state.
//
// Callers provide ordered events and integer HP/damage snapshots. This core owns no actors,
// clocks, input, presentation, or RNG.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace m8
{
constexpr int kExecutionerThresholdPct = 40;
constexpr int kExecutionerBonusPct = 100;
constexpr int kExecutionerRefundPct = 35;
constexpr int kTempoBonusPct = 50;
constexpr int kBulwarkBonusPct = 70;
constexpr int kBulwarkRank1WindowMs = 1750;
constexpr int kBulwarkRank2WindowMs = 2250;
constexpr int kBulwarkRank2Heal = 10;

struct Ranks
{
	int executioner = 0;
	int tempo = 0;
	int bulwark = 0;
};

struct State
{
	int tempo_chain = 0;
	bool counter_active = false;
	std::int64_t counter_expires_at_ms = 0;
	std::uint64_t loadout_fingerprint = 0;
};

struct TargetSnapshot
{
	int current_hp = 0;
	int max_hp = 1;
	double distance_squared = 0.0;
	std::uint32_t spawn_ordinal = 0;
};

struct DamageEventResult
{
	bool tempo_reset = false;
	bool counter_opened = false;
	std::int64_t counter_expires_at_ms = 0;
};

struct SwingInput
{
	bool accepted = true;
	Ranks ranks;
	int base_damage = 0;
	int attack_cooldown_ms = 0;
	int hits = 0;
	std::int64_t now_ms = 0;
	bool has_primary = false;
	int primary_current_hp = 0;
	int primary_max_hp = 1;
	int primary_hp_after_base = 0;
};

struct SwingResult
{
	int executioner_damage = 0;
	int tempo_damage = 0;
	int bulwark_damage = 0;
	int heal = 0;
	int cooldown_refund_ms = 0;
	int primary_hp_after_synergies = 0;
	int tempo_chain_after = 0;
	bool executioner_qualified = false;
	bool executioner_applied = false;
	bool tempo_proc = false;
	bool tempo_applied = false;
	bool tempo_reset_by_miss = false;
	bool counter_consumed = false;
	bool bulwark_applied = false;
};

int clamp_rank(int rank);
int percentage_of(int base, int pct);
bool at_or_below_threshold(int current_hp, int max_hp, int threshold_pct);
std::size_t select_primary(const std::vector<TargetSnapshot>& candidates);
bool sync_loadout(State& state, const Ranks& ranks, std::uint64_t fingerprint);
void reset_transient(State& state);
DamageEventResult on_owner_damage(
	State& state, const Ranks& ranks, int amount, bool enemy_instigated, std::int64_t now_ms);
SwingResult resolve_accepted_swing(State& state, const SwingInput& input);
std::int64_t counter_remaining_ms(const State& state, std::int64_t now_ms);
}
