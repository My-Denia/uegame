// test_m8_build_routes.cpp - engine-independent offer-fixture route gate.

#include "m5_loadout.hpp"
#include "m8_build_synergy.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace
{
int failures = 0;

void check(bool condition, const std::string& name)
{
	if (!condition)
	{
		++failures;
		std::cerr << "FAIL: " << name << "\n";
	}
}

// Duplicates the intended six-affix fixture for deterministic generator and
// synergy reachability checks. It does not verify UE binding, natural play, or Shipping.
std::vector<m5::Affix> six_affix_fixture_pool()
{
	using Kind = m5::AffixKind;
	return {
		{ 1, Kind::DamagePct,          20, 100, 0 },
		{ 2, Kind::DamagePct,          35,  60, 0 },
		{ 3, Kind::AttackIntervalPct, -15, 100, 0 },
		{ 4, Kind::AttackIntervalPct, -25,  60, 0 },
		{ 5, Kind::MaxHpFlat,          30, 100, 0 },
		{ 6, Kind::MaxHpFlat,          60,  60, 0 },
	};
}

std::vector<std::uint32_t> ids_of(const std::vector<m5::Affix>& affixes)
{
	std::vector<std::uint32_t> ids;
	ids.reserve(affixes.size());
	for (const m5::Affix& affix : affixes)
	{
		ids.push_back(affix.id);
	}
	return ids;
}

const m5::Affix* find_affix(const std::vector<m5::Affix>& pool, std::uint32_t id)
{
	const auto it = std::find_if(pool.begin(), pool.end(),
		[id](const m5::Affix& affix) { return affix.id == id; });
	return it == pool.end() ? nullptr : &*it;
}

m8::Ranks family_ranks(
	const std::vector<m5::Affix>& pool, const std::vector<std::uint32_t>& picked_ids)
{
	m8::Ranks ranks;
	for (const std::uint32_t id : picked_ids)
	{
		const m5::Affix* affix = find_affix(pool, id);
		check(affix != nullptr, "picked affix exists in six-affix fixture: " + std::to_string(id));
		if (!affix)
		{
			continue;
		}

		switch (affix->kind)
		{
		case m5::AffixKind::DamagePct:
			ranks.executioner = std::min(2, ranks.executioner + 1);
			break;
		case m5::AffixKind::AttackIntervalPct:
			ranks.tempo = std::min(2, ranks.tempo + 1);
			break;
		case m5::AffixKind::MaxHpFlat:
			ranks.bulwark = std::min(2, ranks.bulwark + 1);
			break;
		case m5::AffixKind::MoveSpeedPct:
			break;
		}
	}
	return ranks;
}

struct RouteRow
{
	const char* name;
	int floor1_choice;
	int floor2_choice;
	std::vector<std::uint32_t> expected_picks;
	m8::Ranks expected_ranks;
};

void verify_route(const std::vector<m5::Affix>& pool, const RouteRow& row)
{
	constexpr std::uint64_t run_seed = 1;
	const std::vector<std::vector<std::uint32_t>> expected_offers = {
		{ 1, 6, 4 },
		{ 5, 1, 4 },
	};
	const int choices[] = { row.floor1_choice, row.floor2_choice };
	std::vector<std::uint32_t> picked_ids;

	for (std::uint64_t floor = 1; floor <= 2; ++floor)
	{
		const std::uint64_t offer_index = floor - 1;
		const std::vector<m5::Affix> offer = m5::generate_offer(
			pool, picked_ids, run_seed, floor, offer_index, 3);
		check(ids_of(offer) == expected_offers[static_cast<std::size_t>(floor - 1)],
			std::string(row.name) + " floor " + std::to_string(floor)
			+ " fixture offer uses offerIndex=floor-1");

		const int choice = choices[floor - 1];
		check(choice >= 0 && static_cast<std::size_t>(choice) < offer.size(),
			std::string(row.name) + " choice is present on floor " + std::to_string(floor));
		if (choice >= 0 && static_cast<std::size_t>(choice) < offer.size())
		{
			picked_ids.push_back(offer[static_cast<std::size_t>(choice)].id);
		}
	}

	check(picked_ids == row.expected_picks, std::string(row.name) + " fixture picked ids");
	const m8::Ranks ranks = family_ranks(pool, picked_ids);
	check(ranks.executioner == row.expected_ranks.executioner
		&& ranks.tempo == row.expected_ranks.tempo
		&& ranks.bulwark == row.expected_ranks.bulwark,
		std::string(row.name) + " picks project to family ranks");
}
}

int main()
{
	const std::vector<m5::Affix> pool = six_affix_fixture_pool();
	const m5::ValidationResult validation = m5::validate_affix_pool(pool);
	check(validation.ok, "six-affix fixture validates");

	const RouteRow routes[] = {
		{ "Executioner R2", 0, 1, { 1, 1 }, { 2, 0, 0 } },
		{ "Tempo R2",       2, 2, { 4, 4 }, { 0, 2, 0 } },
		{ "Bulwark R2",     1, 0, { 6, 5 }, { 0, 0, 2 } },
		{ "E1+B1 hybrid",   0, 0, { 1, 5 }, { 1, 0, 1 } },
	};
	for (const RouteRow& route : routes)
	{
		verify_route(pool, route);
	}

	if (failures != 0)
	{
		std::cerr << failures << " M8 build-route checks failed\n";
		return 1;
	}
	std::cout << "M8 six-affix fixture build routes PASS\n";
	return 0;
}
