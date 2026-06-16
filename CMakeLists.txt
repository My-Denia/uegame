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
