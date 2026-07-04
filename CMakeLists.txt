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
