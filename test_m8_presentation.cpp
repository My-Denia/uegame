#include "m8_presentation.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <set>
#include <string>

int main()
{
	using m8presentation::Cue;
	constexpr std::array<Cue, 9> cues = {
		Cue::AttackMiss, Cue::Hit, Cue::PlayerDamage, Cue::EnemyDeath,
		Cue::RewardOffered, Cue::RewardChosen, Cue::BuildProc,
		Cue::Victory, Cue::Defeat
	};

	std::set<std::uint64_t> signatures;
	std::set<std::string> names;
	for (const Cue cue : cues)
	{
		const auto spec = m8presentation::cue_spec(cue);
		const auto pcm = m8presentation::generate_pcm(cue);
		assert(pcm.sample_rate == 22050);
		assert(pcm.samples.size() == static_cast<std::size_t>((22050 * spec.duration_ms) / 1000));
		assert(pcm.samples.front() == 0);
		assert(pcm.samples.back() == 0);
		assert(std::any_of(pcm.samples.begin(), pcm.samples.end(), [](std::int16_t v) { return v != 0; }));
		assert(signatures.insert(pcm.signature).second);
		assert(names.insert(m8presentation::cue_name(cue)).second);
	}

	const auto offered = m8presentation::generate_pcm(Cue::RewardOffered);
	const auto chosen = m8presentation::generate_pcm(Cue::RewardChosen);
	assert(offered.signature != chosen.signature);
	assert(offered.samples.size() != chosen.samples.size());

	using m8presentation::Overlay;
	assert(m8presentation::choose_overlay(true, true, true, true, true) == Overlay::Result);
	assert(m8presentation::choose_overlay(false, true, true, true, true) == Overlay::Pause);
	assert(m8presentation::choose_overlay(false, false, true, true, true) == Overlay::Contract);
	assert(m8presentation::choose_overlay(false, false, false, true, true) == Overlay::Reward);
	assert(m8presentation::choose_overlay(false, false, false, false, true) == Overlay::Onboarding);
	assert(m8presentation::choose_overlay(false, false, false, false, false) == Overlay::None);

	assert(m8presentation::show_onboarding(1, 0, 0.0));
	assert(m8presentation::show_onboarding(1, 0, 14.999));
	assert(!m8presentation::show_onboarding(1, 0, 15.0));
	assert(!m8presentation::show_onboarding(2, 0, 1.0));
	assert(!m8presentation::show_onboarding(1, 1, 1.0));
	assert(!m8presentation::show_onboarding(1, 0, -0.1));

	std::cout << "m8_presentation: PASS cues=9 rewardDistinct=true sampleRate=22050"
		" overlayPriority=true onboardingBoundary=true\n";
	return 0;
}
