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
