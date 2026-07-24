// test_m8_build_synergy.cpp - table-driven M8A truth-table gate.

#include "m8_build_synergy.hpp"

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
}

int main()
{
	using namespace m8;
	struct RoundRow { int base; int pct; int expected; };
	const RoundRow round_rows[] = {
		{ 1, 50, 1 }, { 15, 50, 8 }, { 18, 100, 18 }, { 15, 70, 11 }, { 600, 35, 210 }
	};
	for (const RoundRow& row : round_rows)
	{
		check(percentage_of(row.base, row.pct) == row.expected,
			"percentage half-up " + std::to_string(row.base) + "x" + std::to_string(row.pct));
	}

	struct ThresholdRow { int hp; int max_hp; bool expected; };
	const ThresholdRow threshold_rows[] = {
		{ 40, 100, true }, { 41, 100, false }, { 8, 20, true }, { 9, 20, false }
	};
	for (const ThresholdRow& row : threshold_rows)
	{
		check(at_or_below_threshold(row.hp, row.max_hp, kExecutionerThresholdPct) == row.expected,
			"threshold " + std::to_string(row.hp) + "/" + std::to_string(row.max_hp));
	}

	std::vector<TargetSnapshot> targets = {
		{ 40, 100, 1.0, 9 }, { 20, 100, 900.0, 8 }, { 10, 50, 100.0, 7 }, { 20, 100, 100.0, 3 }
	};
	check(select_primary(targets) == 3, "primary ratio-distance-ordinal ordering");
	std::vector<TargetSnapshot> ordinal_only = {
		{ 10, 100, 25.0, 4 }, { 20, 200, 25.0, 2 }
	};
	check(select_primary(ordinal_only) == 1, "primary final ordinal tiebreak");
	check(select_primary({}) == static_cast<std::size_t>(-1), "primary empty sentinel");

	State rejected;
	rejected.tempo_chain = 2;
	rejected.counter_active = true;
	rejected.counter_expires_at_ms = 99;
	rejected.loadout_fingerprint = 7;
	SwingInput rejected_input;
	rejected_input.accepted = false;
	rejected_input.ranks = { 2, 2, 2 };
	rejected_input.hits = 1;
	rejected_input.now_ms = 1000;
	resolve_accepted_swing(rejected, rejected_input);
	check(rejected.tempo_chain == 2 && rejected.counter_active
		&& rejected.counter_expires_at_ms == 99 && rejected.loadout_fingerprint == 7,
		"cooldown reject leaves every M8 state field unchanged");

	struct ExecutionerRow
	{
		const char* name;
		int rank;
		int base_damage;
		int pre_hp;
		int max_hp;
		int post_base_hp;
		int expected_damage;
		int expected_final_hp;
		bool applied;
		int refund;
	};
	const ExecutionerRow executioner_rows[] = {
		{ "rank0 absent", 0, 15, 40, 100, 25, 0, 25, false, 0 },
		{ "above threshold", 1, 15, 41, 100, 26, 0, 26, false, 0 },
		{ "threshold equality", 1, 15, 40, 100, 25, 15, 10, true, 0 },
		{ "floor2 Brute 90HP pre36 base18 kill", 1, 18, 36, 90, 18, 18, 0, true, 0 },
		{ "base killed no retarget", 1, 15, 10, 100, 0, 0, 0, false, 0 },
		{ "rank2 qualifying base kill refund", 2, 15, 20, 100, 0, 0, 0, false, 210 },
		{ "rank2 qualifying kill refund", 2, 15, 20, 100, 5, 15, 0, true, 210 },
		{ "rank2 non-kill no refund", 2, 15, 40, 100, 30, 15, 15, true, 0 }
	};
	for (const ExecutionerRow& row : executioner_rows)
	{
		State state;
		SwingInput in;
		in.ranks.executioner = row.rank;
		in.base_damage = row.base_damage;
		in.attack_cooldown_ms = 600;
		in.hits = 1;
		in.has_primary = true;
		in.primary_current_hp = row.pre_hp;
		in.primary_max_hp = row.max_hp;
		in.primary_hp_after_base = row.post_base_hp;
		const SwingResult out = resolve_accepted_swing(state, in);
		check(out.executioner_applied == row.applied, std::string("executioner ") + row.name);
		check(out.executioner_damage == row.expected_damage,
			std::string("executioner damage ") + row.name);
		check(out.primary_hp_after_synergies == row.expected_final_hp,
			std::string("executioner final hp ") + row.name);
		check(out.cooldown_refund_ms == row.refund, std::string("executioner refund ") + row.name);
	}

	State tempo;
	SwingInput ti;
	ti.ranks.tempo = 1;
	ti.base_damage = 15;
	ti.has_primary = true;
	ti.primary_current_hp = 100;
	ti.primary_max_hp = 100;
	ti.primary_hp_after_base = 85;
	ti.hits = 4;
	check(!resolve_accepted_swing(tempo, ti).tempo_proc && tempo.tempo_chain == 1,
		"tempo advances once per multi-hit swing");
	ti.hits = 1;
	check(!resolve_accepted_swing(tempo, ti).tempo_proc && tempo.tempo_chain == 2,
		"tempo rank1 second clean hit");
	const SwingResult tempo_proc = resolve_accepted_swing(tempo, ti);
	check(tempo_proc.tempo_proc && tempo_proc.tempo_damage == 8 && tempo.tempo_chain == 0,
		"tempo rank1 third hit proc half-up");

	tempo.tempo_chain = 2;
	ti.hits = 0;
	ti.has_primary = false;
	const SwingResult miss = resolve_accepted_swing(tempo, ti);
	check(miss.tempo_reset_by_miss && tempo.tempo_chain == 0, "tempo miss reset");
	tempo.tempo_chain = 2;
	check(!on_owner_damage(tempo, ti.ranks, 0, false, 10).tempo_reset && tempo.tempo_chain == 2,
		"zero owner damage inert");
	check(on_owner_damage(tempo, ti.ranks, 1, false, 10).tempo_reset && tempo.tempo_chain == 0,
		"positive non-enemy damage resets tempo only");

	State tempo2;
	ti.ranks.tempo = 2;
	ti.hits = 1;
	ti.has_primary = true;
	ti.primary_hp_after_base = 20;
	resolve_accepted_swing(tempo2, ti);
	ti.primary_hp_after_base = 0;
	const SwingResult spent = resolve_accepted_swing(tempo2, ti);
	check(spent.tempo_proc && !spent.tempo_applied && tempo2.tempo_chain == 0,
		"tempo rank2 dead-primary proc spent");

	State bulwark;
	Ranks br;
	br.bulwark = 1;
	check(!on_owner_damage(bulwark, br, 5, false, 1000).counter_opened,
		"bulwark non-enemy damage does not arm");
	DamageEventResult opened = on_owner_damage(bulwark, br, 5, true, 1000);
	check(opened.counter_opened && opened.counter_expires_at_ms == 2750,
		"bulwark rank1 1750ms window");
	opened = on_owner_damage(bulwark, br, 5, true, 1500);
	check(opened.counter_expires_at_ms == 3250, "bulwark hit replaces window");
	SwingInput bi;
	bi.ranks = br;
	bi.base_damage = 15;
	bi.hits = 1;
	bi.now_ms = 3250;
	bi.has_primary = true;
	bi.primary_current_hp = 100;
	bi.primary_max_hp = 100;
	bi.primary_hp_after_base = 85;
	const SwingResult equality = resolve_accepted_swing(bulwark, bi);
	check(equality.counter_consumed && equality.bulwark_damage == 11,
		"bulwark expiry equality valid and consumed");

	on_owner_damage(bulwark, br, 5, true, 4000);
	bi.now_ms = 5751;
	const SwingResult expired = resolve_accepted_swing(bulwark, bi);
	check(!expired.counter_consumed && !bulwark.counter_active, "bulwark Now greater than expiry invalid");

	on_owner_damage(bulwark, br, 5, true, 6000);
	bi.hits = 0;
	bi.has_primary = false;
	bi.now_ms = 6100;
	check(!resolve_accepted_swing(bulwark, bi).counter_consumed && bulwark.counter_active,
		"bulwark miss preserves active counter");

	State bulwark2;
	br.bulwark = 2;
	opened = on_owner_damage(bulwark2, br, 5, true, 1000);
	check(opened.counter_expires_at_ms == 3250, "bulwark rank2 2250ms window");
	bi.ranks = br;
	bi.hits = 1;
	bi.has_primary = true;
	bi.primary_hp_after_base = 0;
	bi.now_ms = 1100;
	const SwingResult healed = resolve_accepted_swing(bulwark2, bi);
	check(healed.counter_consumed && healed.heal == 10 && !healed.bulwark_applied,
		"bulwark rank2 consume heals on dead primary without retarget");

	State hybrid;
	hybrid.tempo_chain = 2;
	hybrid.counter_active = true;
	hybrid.counter_expires_at_ms = 5000;
	SwingInput hi;
	hi.ranks = { 2, 1, 2 };
	hi.base_damage = 15;
	hi.attack_cooldown_ms = 600;
	hi.hits = 2;
	hi.now_ms = 1000;
	hi.has_primary = true;
	hi.primary_current_hp = 20;
	hi.primary_max_hp = 100;
	hi.primary_hp_after_base = 5;
	const SwingResult hybrid_out = resolve_accepted_swing(hybrid, hi);
	check(hybrid_out.executioner_applied && hybrid_out.tempo_proc && !hybrid_out.tempo_applied
		&& hybrid_out.counter_consumed && !hybrid_out.bulwark_applied
		&& hybrid_out.heal == 10 && hybrid_out.cooldown_refund_ms == 210,
		"synthetic E2/T1/B2 stress: strict E-T-B ordering and dead-primary spend");

	State warden;
	warden.tempo_chain = 1;
	warden.counter_active = true;
	warden.counter_expires_at_ms = 5000;
	SwingInput wi;
	wi.ranks = { 1, 2, 2 };
	wi.base_damage = 15;
	wi.attack_cooldown_ms = 600;
	wi.hits = 1;
	wi.now_ms = 1000;
	wi.has_primary = true;
	wi.primary_current_hp = 30;
	wi.primary_max_hp = 120;
	wi.primary_hp_after_base = 300;
	const SwingPlan warden_plan = plan_accepted_swing(warden, wi);
	check(warden_plan.executioner_damage == 15 && warden_plan.tempo_damage == 8
		&& warden_plan.bulwark_damage == 11 && warden_plan.heal_request == 10,
		"guard-break swing emits raw E15 T8 B11 intents and Bulwark heal request");
	SwingActual warden_actual;
	warden_actual.executioner_damage = 23;
	warden_actual.tempo_damage = 12;
	warden_actual.bulwark_damage = 17;
	warden_actual.primary_pool_after = 248;
	const SwingResult warden_settled = reconcile_accepted_swing(warden_plan, warden_actual);
	check(warden_settled.executioner_applied && warden_settled.tempo_applied
		&& warden_settled.bulwark_applied && warden_settled.heal == 10
		&& warden_settled.primary_hp_after_synergies == 248,
		"authoritative Warden reconciliation binds actual E23 T12 B17 once");

	SwingPlan kill_plan = warden_plan;
	kill_plan.executioner_rank = 2;
	SwingActual killed_late;
	killed_late.tempo_damage = 5;
	killed_late.primary_dead_after = true;
	const SwingResult killed_late_result = reconcile_accepted_swing(kill_plan, killed_late);
	check(killed_late_result.cooldown_refund_ms == 210 && !killed_late_result.executioner_applied,
		"Executioner refund uses pre-pool qualification plus authoritative later-proc death");

	SwingPlan dead_primary_counter;
	dead_primary_counter.counter_consumed = true;
	dead_primary_counter.heal_request = 10;
	const SwingResult dead_primary_settled = reconcile_accepted_swing(
		dead_primary_counter, SwingActual{});
	check(dead_primary_settled.counter_consumed && dead_primary_settled.heal == 10
		&& !dead_primary_settled.bulwark_applied,
		"Bulwark heal survives base-killed primary when the armed counter was consumed");

	SwingPlan ordinary_counter;
	ordinary_counter.counter_consumed = true;
	ordinary_counter.bulwark_damage = 11;
	ordinary_counter.heal_request = 10;
	SwingActual ordinary_actual;
	ordinary_actual.bulwark_damage = 11;
	const SwingResult ordinary_settled = reconcile_accepted_swing(ordinary_counter, ordinary_actual);
	check(ordinary_settled.bulwark_applied && ordinary_settled.bulwark_damage == 11
		&& ordinary_settled.heal == 10,
		"ordinary counter parity keeps raw and actual 11 plus heal request");

	State sync;
	sync.tempo_chain = 2;
	sync.counter_active = true;
	sync.counter_expires_at_ms = 99;
	sync.loadout_fingerprint = 10;
	check(!sync_loadout(sync, { 1, 0, 0 }, 10) && sync.tempo_chain == 2,
		"same fingerprint preserves transient state");
	check(sync_loadout(sync, { 1, 1, 0 }, 11) && sync.tempo_chain == 0 && !sync.counter_active,
		"fingerprint change resets transient state");
	sync.tempo_chain = 1;
	check(sync_loadout(sync, {}, 0) && sync.loadout_fingerprint == 0 && sync.tempo_chain == 0,
		"empty picks reset transient state");

	if (failures != 0)
	{
		std::cerr << failures << " M8 build-synergy checks failed\n";
		return 1;
	}
	std::cout << "M8 build-synergy truth table PASS\n";
	return 0;
}
