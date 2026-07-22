// m8_enemy_behavior.hpp - RNG-free bounded M8 enemy phase rules.
//
// The caller owns movement, collision, navigation and presentation. This core owns only
// phase/timing truth and emits one-shot commit intents.

#pragma once

#include <cstdint>

namespace m8enemy
{
constexpr std::int64_t kGruntWindupMs = 450;
constexpr std::int64_t kGruntRecoveryMs = 650;
constexpr std::int64_t kRunnerCircleMs = 1250;
constexpr std::int64_t kRunnerTellMs = 350;
constexpr std::int64_t kRunnerDashMaxMs = 900;
constexpr std::int64_t kRunnerDisengageMs = 900;
constexpr std::int64_t kBruteTellMs = 900;
constexpr std::int64_t kBruteRecoveryMs = 1200;
constexpr double kRunnerCircleRadius = 325.0;
constexpr double kRunnerDashCap = 450.0;
constexpr double kBruteDangerRadius = 240.0;
constexpr int kContactDamagePercent = 75;

enum class Archetype : std::int32_t
{
	Grunt = 0,
	Runner = 1,
	Brute = 2
};

enum class Phase : std::int32_t
{
	Dormant = 0,
	Approach,
	GruntWindup,
	RunnerCircle,
	RunnerTell,
	RunnerDash,
	RunnerDisengage,
	BruteTell,
	Recovery,
	Dead
};

struct Vec2
{
	double x = 0.0;
	double y = 0.0;
};

struct State
{
	Archetype archetype = Archetype::Grunt;
	Phase phase = Phase::Dormant;
	std::int64_t phase_started_ms = 0;
	std::uint32_t spawn_ordinal = 0;
	int circle_direction = 1;
	Vec2 dash_target;
	bool dash_target_valid = false;
	std::uint32_t commit_serial = 0;
};

struct Input
{
	std::int64_t now_ms = 0;
	bool alive = true;
	bool active = false;
	bool within_leash = true;
	bool has_los = true;
	bool nav_valid = true;
	bool in_attack_range = false;
	bool in_danger_radius = false;
	bool dash_complete = false;
	/** Phase-duration ratio. Default 1/1 preserves M8 2A byte behavior; the room-contract
	 * Challenge uses 5/6 with integer half-up quantization. Invalid ratios fail closed to 1/1. */
	std::int32_t duration_numerator = 1;
	std::int32_t duration_denominator = 1;
	Vec2 self;
	Vec2 target;
};

struct Output
{
	Phase previous_phase = Phase::Dormant;
	Phase phase = Phase::Dormant;
	bool phase_changed = false;
	bool strike_committed = false;
	bool request_hit = false;
	bool attack_cancelled = false;
	bool dash_started = false;
	bool reset = false;
	std::uint32_t commit_serial = 0;
	Vec2 dash_target;
	int circle_direction = 1;
};

Archetype clamp_archetype(int type_id);
int scale_contact_damage(int base_damage, int percent = kContactDamagePercent);
int runner_circle_direction(std::uint32_t spawn_ordinal);
Vec2 cap_dash_target(Vec2 self, Vec2 target);
void initialize(State& state, Archetype archetype, std::uint32_t spawn_ordinal, std::int64_t now_ms);
void reset(State& state, std::int64_t now_ms, bool dead = false);
Output update(State& state, const Input& input);
const char* phase_name(Phase phase);
}
