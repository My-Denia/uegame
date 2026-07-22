// m8_presentation.cpp - see header.

#include "m8_presentation.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace m8presentation
{
namespace
{
	constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
	constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

	std::uint64_t signature_of(Cue cue, const std::vector<std::int16_t>& samples)
	{
		std::uint64_t hash = kFnvOffset;
		hash ^= static_cast<std::uint8_t>(cue);
		hash *= kFnvPrime;
		for (const std::int16_t sample : samples)
		{
			const std::uint16_t bits = static_cast<std::uint16_t>(sample);
			hash ^= static_cast<std::uint8_t>(bits & 0xffU);
			hash *= kFnvPrime;
			hash ^= static_cast<std::uint8_t>((bits >> 8U) & 0xffU);
			hash *= kFnvPrime;
		}
		return hash;
	}
}

CueSpec cue_spec(Cue cue)
{
	switch (cue)
	{
	case Cue::AttackMiss:    return { 130, 520, 180, 1, 7000 };
	case Cue::Hit:           return { 100, 190, 100, 1, 12000 };
	case Cue::PlayerDamage:  return { 220, 125, 62, 2, 13000 };
	case Cue::EnemyDeath:    return { 300, 330, 72, 2, 12000 };
	case Cue::RewardOffered: return { 260, 440, 660, 2, 9000 };
	case Cue::RewardChosen:  return { 360, 660, 1040, 3, 10000 };
	case Cue::BuildProc:     return { 280, 880, 1320, 3, 10500 };
	case Cue::Victory:       return { 720, 523, 1046, 4, 11000 };
	case Cue::Defeat:        return { 650, 220, 55, 3, 11500 };
	case Cue::Count:
	default:                 return {};
	}
}

const char* cue_name(Cue cue)
{
	switch (cue)
	{
	case Cue::AttackMiss: return "attack-miss";
	case Cue::Hit: return "hit";
	case Cue::PlayerDamage: return "player-damage";
	case Cue::EnemyDeath: return "enemy-death";
	case Cue::RewardOffered: return "reward-offered";
	case Cue::RewardChosen: return "reward-chosen";
	case Cue::BuildProc: return "build-proc";
	case Cue::Victory: return "victory";
	case Cue::Defeat: return "defeat";
	case Cue::Count:
	default: return "invalid";
	}
}

PcmBuffer generate_pcm(Cue cue, int sample_rate)
{
	PcmBuffer out;
	out.sample_rate = std::max(8000, sample_rate);
	const CueSpec spec = cue_spec(cue);
	const int sample_count = std::max(2, (out.sample_rate * spec.duration_ms) / 1000);
	out.samples.resize(static_cast<std::size_t>(sample_count), 0);

	std::uint64_t phase = 0;
	const int attack_samples = std::max(1, out.sample_rate / 200);  // 5 ms
	const int release_samples = std::max(1, out.sample_rate / 30); // ~33 ms
	for (int i = 0; i < sample_count; ++i)
	{
		const std::int64_t frequency = spec.start_hz
			+ (static_cast<std::int64_t>(spec.end_hz - spec.start_hz) * i)
				/ std::max(1, sample_count - 1);
		phase += (static_cast<std::uint64_t>(std::max<std::int64_t>(1, frequency)) << 32U)
			/ static_cast<std::uint64_t>(out.sample_rate);

		const std::uint32_t unit = static_cast<std::uint32_t>((phase >> 16U) & 0xffffU);
		const std::int32_t triangle = unit < 32768U
			? static_cast<std::int32_t>(unit * 2U) - 32768
			: static_cast<std::int32_t>((65535U - unit) * 2U) - 32768;

		const int attack = std::min(32767, (i * 32767) / attack_samples);
		const int release = std::min(32767, ((sample_count - 1 - i) * 32767) / release_samples);
		int envelope = std::min(attack, release);

		// Separate multi-pulse cues with a short deterministic valley. This produces
		// distinct readable rhythms without floating point or external wave assets.
		if (spec.pulse_count > 1)
		{
			const int pulse_position = (i * spec.pulse_count * 100) / sample_count;
			if ((pulse_position % 100) >= 82)
			{
				envelope /= 6;
			}
		}

		const std::int64_t scaled = static_cast<std::int64_t>(triangle)
			* spec.amplitude * envelope / (32768LL * 32767LL);
		out.samples[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(
			std::clamp<std::int64_t>(scaled, -32768, 32767));
	}

	// Exact silent endpoints prevent clicks and give the tests a format-independent invariant.
	out.samples.front() = 0;
	out.samples.back() = 0;
	out.signature = signature_of(cue, out.samples);
	return out;
}
} // namespace m8presentation
