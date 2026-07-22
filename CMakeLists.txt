cmake_minimum_required(VERSION 3.10)
project(dungeon_layout CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(dungeon main.cpp)
# dungeon.hpp is header-only; listed so IDEs pick it up.
target_sources(dungeon PRIVATE dungeon.hpp)

# --- Engine-agnostic verification executables (the same sources g++ builds by hand) ---
# m1verify: M1 core validate + byte-for-byte regenerate-determinism (public API only).
add_executable(m1verify m1_verify.cpp)
target_sources(m1verify PRIVATE dungeon.hpp)

# m2test: M2/M3/M4 adapter CLI. Its validate/determinism modes return 0/1 exit codes.
add_executable(m2test m2_adapter_test.cpp)
target_sources(m2test PRIVATE m2_adapter.hpp dungeon.hpp)

# --- M5 build-diversity: engine-agnostic loadout core (PR #12A; no UE, no gameplay change) ---
# m5_loadout_test: determinism + fuzz gate (14 counted sub-checks; exit 0 iff all pass).
# Links the core .cpp; the test TU also includes m2_adapter.hpp to PROVE loadout draws leave
# the M1/M3/M4 seed streams bit-identical (isolation sub-check).
add_executable(m5_loadout_test test_m5_loadout.cpp m5_loadout.cpp)
target_sources(m5_loadout_test PRIVATE m5_loadout.hpp m2_adapter.hpp dungeon.hpp)

# m5_loadout_golden: emits the byte-stable golden vector consumed by diff_golden.cmake.
# The core + this dumper never include m2_adapter.hpp (they are fully engine/loadout-independent).
add_executable(m5_loadout_golden dump_m5_loadout_golden.cpp m5_loadout.cpp)
target_sources(m5_loadout_golden PRIVATE m5_loadout.hpp)

# --- CTest: the pre-registered determinism/reachability checks as re-runnable gates ---
# Turns "verified" from close-out prose into a repo artifact anyone can re-run.
enable_testing()
# M1 grid: validate + regenerate-determinism across 1000 seeds (exit 0 iff all pass).
add_test(NAME m1_validate          COMMAND m1verify 1000)
# M2 world: grid<->world full-connectivity + consistency across 200 seeds.
add_test(NAME m2_world_validate    COMMAND m2test validate 200)
# M3 enemies: 2x-generation identical plans + in-room/start-excluded/no-dup over 500 seeds.
add_test(NAME m3_enemy_determinism COMMAND m2test enemyDeterminism 500)
# M4 floors: same runSeed => identical floor sequence; adjacent runSeed differs, 200 seeds.
add_test(NAME m4_floor_determinism COMMAND m2test floorsDeterminism 200)
# M5 loadout: determinism + fuzz (14 counted sub-checks incl. domain isolation; ~0.5s).
add_test(NAME m5_loadout_determinism COMMAND m5_loadout_test)
# M5 golden: strict byte-compare of the regenerated golden vector vs the committed one.
# Cross-compiler proof: the golden is authored by g++ (== CI); this rebuilds it with the local
# toolchain and requires byte-identical output.
add_test(NAME m5_loadout_goldenvec
         COMMAND ${CMAKE_COMMAND}
                 -DDUMPER=$<TARGET_FILE:m5_loadout_golden>
                 -DGOLDEN=${CMAKE_CURRENT_SOURCE_DIR}/golden_m5_loadout_seed7.txt
                 -DACTUAL=${CMAKE_CURRENT_BINARY_DIR}/m5_loadout_golden.actual.txt
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/diff_golden.cmake)

# --- M6A encounter-diversity: engine-agnostic core (batch 1; no UE, no gameplay change) ---
# m6_encounter_test: determinism + fuzz + anchor gate (16 counted sub-checks; exit 0 iff all pass).
# The test TU includes dungeon.hpp + m2_adapter.hpp only to build the seed-7 witness and PROVE
# the frozen M1/M2 anchors (spawnPlanHash/enemyPlanHash) are unperturbed; the core never does.
# CSV paths are passed as args so ctest (run from the build dir) reads the committed source data.
add_executable(m6_encounter_test test_m6_encounter.cpp m6_encounter.cpp)
target_sources(m6_encounter_test PRIVATE m6_encounter.hpp m2_adapter.hpp dungeon.hpp)

# m6_encounter_golden: emits the byte-stable golden vector consumed by diff_m6_golden.cmake.
# Data-driven: reads EncounterWeights.csv (the committed weights are the single source of truth).
add_executable(m6_encounter_golden dump_m6_encounter_golden.cpp m6_encounter.cpp)
target_sources(m6_encounter_golden PRIVATE m6_encounter.hpp m2_adapter.hpp dungeon.hpp)

# M6A encounter: determinism + fuzz + no-density/no-position + anchor preservation (~1s).
add_test(NAME m6_encounter_determinism
         COMMAND m6_encounter_test
                 ${CMAKE_CURRENT_SOURCE_DIR}/uegame/Content/Data/EnemyArchetypes.csv
                 ${CMAKE_CURRENT_SOURCE_DIR}/uegame/Content/Data/EncounterWeights.csv
                 ${CMAKE_CURRENT_SOURCE_DIR}/uegame/Content/Data/CombatConfig.csv)
# M6A golden: strict byte-compare of the regenerated (data-driven) golden vs the committed one.
add_test(NAME m6_encounter_goldenvec
         COMMAND ${CMAKE_COMMAND}
                 -DDUMPER=$<TARGET_FILE:m6_encounter_golden>
                 -DGOLDEN=${CMAKE_CURRENT_SOURCE_DIR}/golden_m6_encounter_seed7.txt
                 -DACTUAL=${CMAKE_CURRENT_BINARY_DIR}/m6_encounter_golden.actual.txt
                 -DWEIGHTS=${CMAKE_CURRENT_SOURCE_DIR}/uegame/Content/Data/EncounterWeights.csv
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/diff_m6_golden.cmake)

# --- M8A build synergy: RNG-free ordered combat rules/state (additive; M1-M7 frozen) ---
add_executable(m8_build_synergy_test test_m8_build_synergy.cpp m8_build_synergy.cpp)
target_sources(m8_build_synergy_test PRIVATE m8_build_synergy.hpp)
add_test(NAME m8_build_synergy COMMAND m8_build_synergy_test)

# --- M8 2A enemy behavior: bounded phase/timing core (additive; M1-M7 frozen) ---
add_executable(m8_enemy_behavior_test test_m8_enemy_behavior.cpp m8_enemy_behavior.cpp)
target_sources(m8_enemy_behavior_test PRIVATE m8_enemy_behavior.hpp)
add_test(NAME m8_enemy_behavior COMMAND m8_enemy_behavior_test)

# --- M8 objective compass: engine-independent candidate ordering + eight-way direction core ---
add_executable(m8_objective_compass_test test_m8_objective_compass.cpp m8_objective_compass.cpp)
target_sources(m8_objective_compass_test PRIVATE m8_objective_compass.hpp)
add_test(NAME m8_objective_compass COMMAND m8_objective_compass_test)

# --- M8 build-route reachability over a six-affix fixture + M8 synergy core ---
add_executable(test_m8_build_routes test_m8_build_routes.cpp m5_loadout.cpp m8_build_synergy.cpp)
target_sources(test_m8_build_routes PRIVATE m5_loadout.hpp m8_build_synergy.hpp)
target_compile_options(test_m8_build_routes PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
add_test(NAME m8_build_routes COMMAND test_m8_build_routes)

# --- M8 room flow: role-clear healing + post-pick recovery (additive; no RNG) ---
add_executable(m8_room_flow_test test_m8_room_flow.cpp m8_room_flow.cpp m8_room_contract.cpp)
target_sources(m8_room_flow_test PRIVATE m8_room_flow.hpp m8_room_contract.hpp)
add_test(NAME m8_room_flow COMMAND m8_room_flow_test)

# --- M8 finale: deterministic Warden selection, guard and exposed-window arithmetic ---
add_executable(m8_finale_test test_m8_finale.cpp m8_finale.cpp)
target_sources(m8_finale_test PRIVATE m8_finale.hpp)
add_test(NAME m8_finale COMMAND m8_finale_test)

# --- M8 runtime authority: engine-independent exit/reward and lifecycle policy ---
add_executable(m8_runtime_authority_test
               test_m8_runtime_authority.cpp
               m8_runtime_authority.cpp)
target_sources(m8_runtime_authority_test PRIVATE m8_runtime_authority.hpp)
add_test(NAME m8_runtime_authority COMMAND m8_runtime_authority_test)
