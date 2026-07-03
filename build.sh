#!/usr/bin/env bash
# Self-contained build: standard C++17, no dependencies beyond a compiler.
# Usage: ./build.sh   ->   produces ./dungeon
set -euo pipefail

CXX="${CXX:-g++}"
"$CXX" -std=c++17 -O2 -Wall -Wextra -Wpedantic -o dungeon main.cpp
echo "built ./dungeon"
