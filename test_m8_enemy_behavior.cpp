// test_m8_enemy_behavior.cpp - table/boundary gate for the bounded M8 enemy core.

#include "m8_enemy_behavior.hpp"

#include <cmath>
#include <iostream>
#include <string>

namespace
{
int failures = 0;
void check(bool condition, const std::string& name)
{
	if (!condition)
	{
		++failures;
		std::cerr << "FAIL: " << name << std::endl;
	}
}

m8enemy::Input active_at(std::int64_t now)
{
	m8enemy::Input in;
	in.now_ms = now;
	in.active = true;
	return in;
}
}

int main()
{
	using namespace m8enemy;

	check(runner_circle_direction(0) == 1 && runner_circle_direction(2) == 1,
		"runner even ordinal clockwise");
	check(runner_circle_direction(1) == -1 && runner_circle_direction(3) == -1,
		"runner odd ordinal counter-clockwise");
	check(clamp_archetype(-1) == Archetype::Grunt && clamp_archetype(99) == Archetype::Grunt,
		"invalid archetype clamps to grunt");
	check(scale_contact_damage(7) == 5, "grunt assigned 7 becomes committed 5");
	check(scale_contact_damage(5) == 4, "runner assigned 5 becomes committed 4");
	check(scale_contact_damage(10) == 8, "brute assigned 10 becomes committed 8");
	check(scale_contact_damage(3, 50) == 2, "contact scaling rounds half up");
	check(scale_contact_damage(1, 1) == 1, "positive assigned damage stays at least one");
	check(scale_contact_damage(0) == 0 && scale_contact_damage(-1) == 0,
		"non-positive assigned damage stays zero");

	State grunt;
	initialize(grunt, Archetype::Grunt, 4, 0);
	Input in = active_at(0);
	check(update(grunt, in).phase == Phase::Approach, "grunt activates to approach");
	in.in_attack_range = true;
	check(update(grunt, in).phase == Phase::GruntWindup, "grunt enters windup");
	in.now_ms = 449;
	check(!update(grunt, in).strike_committed && grunt.phase == Phase::GruntWindup,
		"grunt windup below boundary");
	in.now_ms = 450;
	Output out = update(grunt, in);
	check(out.strike_committed && out.request_hit && out.commit_serial == 1
		&& grunt.phase == Phase::Recovery, "grunt equality commits one narrow hit");
	check(!update(grunt, in).strike_committed && grunt.commit_serial == 1,
		"grunt repeated update cannot double-hit commit");
	in.now_ms = 1099;
	check(update(grunt, in).phase == Phase::Recovery, "grunt recovery below boundary");
	in.now_ms = 1100;
	check(update(grunt, in).phase == Phase::Approach, "grunt recovery equality");

	State challenge_grunt;
	initialize(challenge_grunt, Archetype::Grunt, 6, 0);
	in = active_at(0);
	in.duration_numerator = 5;
	in.duration_denominator = 6;
	update(challenge_grunt, in);
	in.in_attack_range = true;
	update(challenge_grunt, in);
	in.now_ms = 374;
	check(!update(challenge_grunt, in).strike_committed,
		"challenge grunt windup below exact five-sixths boundary");
	in.now_ms = 375;
	check(update(challenge_grunt, in).strike_committed,
		"challenge grunt commits at exact five-sixths boundary");

	State range_miss;
	initialize(range_miss, Archetype::Grunt, 0, 0);
	in = active_at(0);
	update(range_miss, in);
	in.in_attack_range = true;
	update(range_miss, in);
	in.now_ms = 450;
	in.in_attack_range = false;
	out = update(range_miss, in);
	check(out.strike_committed && !out.request_hit && out.attack_cancelled,
		"grunt moved out of narrow range misses");

	State los_miss;
	initialize(los_miss, Archetype::Grunt, 0, 0);
	in = active_at(0);
	update(los_miss, in);
	in.in_attack_range = true;
	update(los_miss, in);
	in.now_ms = 450;
	in.has_los = false;
	out = update(los_miss, in);
	check(out.strike_committed && !out.request_hit && out.attack_cancelled,
		"grunt wall at commit prevents hit");

	State leash;
	initialize(leash, Archetype::Grunt, 0, 0);
	in = active_at(0);
	update(leash, in);
	in.in_attack_range = true;
	update(leash, in);
	in.now_ms = 100;
	in.within_leash = false;
	out = update(leash, in);
	check(out.reset && leash.phase == Phase::Dormant && leash.commit_serial == 0,
		"leash exit resets committed phase state");

	State runner;
	initialize(runner, Archetype::Runner, 2, 0);
	in = active_at(0);
	check(update(runner, in).phase == Phase::RunnerCircle, "runner activates to circle");
	in.now_ms = 1249;
	check(update(runner, in).phase == Phase::RunnerCircle, "runner circle below 1250");
	in.now_ms = 1250;
	check(update(runner, in).phase == Phase::RunnerTell, "runner circle equality 1250");
	in.now_ms = 1599;
	check(update(runner, in).phase == Phase::RunnerTell, "runner tell below 350");
	in.now_ms = 1600;
	in.self = { 0.0, 0.0 };
	in.target = { 600.0, 0.0 };
	out = update(runner, in);
	check(out.dash_started && runner.phase == Phase::RunnerDash
		&& std::abs(runner.dash_target.x - 450.0) < 0.001
		&& std::abs(runner.dash_target.y) < 0.001, "runner tell snapshots capped 450 dash");
	in.now_ms = 1700;
	in.target = { 0.0, 600.0 };
	update(runner, in);
	check(std::abs(runner.dash_target.x - 450.0) < 0.001
		&& std::abs(runner.dash_target.y) < 0.001, "runner dash target remains snapshotted");
	in.now_ms = 1701;
	in.nav_valid = false;
	out = update(runner, in);
	check(out.attack_cancelled && runner.phase == Phase::RunnerDisengage
		&& !runner.dash_target_valid, "runner nav loss cancels dash");

	State runner_hit;
	initialize(runner_hit, Archetype::Runner, 1, 0);
	in = active_at(0);
	update(runner_hit, in);
	in.now_ms = 1250;
	update(runner_hit, in);
	in.now_ms = 1600;
	in.self = { 10.0, 20.0 };
	in.target = { 110.0, 120.0 };
	update(runner_hit, in);
	check(std::abs(runner_hit.dash_target.x - 110.0) < 0.001
		&& std::abs(runner_hit.dash_target.y - 120.0) < 0.001,
		"runner under-cap target unchanged");
	in.now_ms = 1650;
	in.dash_complete = true;
	in.in_attack_range = true;
	out = update(runner_hit, in);
	check(out.strike_committed && out.request_hit && out.commit_serial == 1
		&& runner_hit.phase == Phase::RunnerDisengage, "runner dash completes one strike");
	check(!update(runner_hit, in).strike_committed && runner_hit.commit_serial == 1,
		"runner strike cannot repeat");
	in.now_ms = 2549;
	check(update(runner_hit, in).phase == Phase::RunnerDisengage, "runner disengage below 900");
	in.now_ms = 2550;
	check(update(runner_hit, in).phase == Phase::RunnerCircle, "runner disengage equality 900");

	State runner_cancel;
	initialize(runner_cancel, Archetype::Runner, 0, 0);
	in = active_at(0);
	update(runner_cancel, in);
	in.now_ms = 1250;
	update(runner_cancel, in);
	in.now_ms = 1600;
	in.nav_valid = false;
	out = update(runner_cancel, in);
	check(out.attack_cancelled && !out.dash_started
		&& runner_cancel.phase == Phase::RunnerDisengage, "runner invalid nav cancels tell");

	State brute;
	initialize(brute, Archetype::Brute, 8, 0);
	in = active_at(0);
	update(brute, in);
	in.in_danger_radius = true;
	check(update(brute, in).phase == Phase::BruteTell, "brute enters slam tell");
	in.now_ms = 899;
	check(!update(brute, in).strike_committed, "brute tell below 900");
	in.now_ms = 900;
	out = update(brute, in);
	check(out.strike_committed && out.request_hit && brute.phase == Phase::Recovery,
		"brute equality commits area hit");
	in.now_ms = 2099;
	check(update(brute, in).phase == Phase::Recovery, "brute recovery below 1200");
	in.now_ms = 2100;
	check(update(brute, in).phase == Phase::Approach, "brute recovery equality 1200");

	in.alive = false;
	out = update(brute, in);
	check(out.reset && brute.phase == Phase::Dead && brute.commit_serial == 0
		&& !brute.dash_target_valid, "death clears all transient behavior state");
	reset(brute, 3000);
	check(brute.phase == Phase::Dormant && brute.commit_serial == 0,
		"explicit reset returns clean dormant state");

	std::cout << "m8_enemy_behavior: "
		<< (failures == 0 ? "PASS" : "FAIL") << " (" << failures << " failures)"
		<< std::endl;
	return failures == 0 ? 0 : 1;
}
