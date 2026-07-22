// m8_presentation.hpp - deterministic, engine-independent event tone generation.

#pragma once

#include <cstdint>
#include <vector>

namespace m8presentation
{
enum class Cue : std::uint8_t
{
	AttackMiss = 0,
	Hit,
	PlayerDamage,
	EnemyDeath,
	RewardOffered,
	RewardChosen,
	BuildProc,
	Victory,
	Defeat,
	Count
};

struct CueSpec
{
	int duration_ms = 100;
	int start_hz = 440;
	int end_hz = 440;
	int pulse_count = 1;
	int amplitude = 8000;
};

struct PcmBuffer
{
	int sample_rate = 22050;
	std::vector<std::int16_t> samples;
	std::uint64_t signature = 0;
};

CueSpec cue_spec(Cue cue);
const char* cue_name(Cue cue);
PcmBuffer generate_pcm(Cue cue, int sample_rate = 22050);
} // namespace m8presentation
