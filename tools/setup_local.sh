#!/usr/bin/env bash
# Developer setup: configure, build (extracting the assets from game/ the first time) and run the tests.
#   tools/setup_local.sh
# Needs cmake, a C++17 compiler, python3 with `pip install -r requirements.txt`. See docs/building.md.
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release ${CMAKE_ARGS:-}
cmake --build build -j
ctest --test-dir build --output-on-failure
echo
echo "Run:  ./build/sbso --extracted build/game.pak        (or open the app: see docs/building.md)"
