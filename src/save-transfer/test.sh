#!/bin/bash
# Builds and runs the Mac-side test of the save logic against Chris's real saves and stored cars.
set -euo pipefail
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}/nfsu2_save_transfer_test"
clang++ -std=c++17 -O1 -Wall -Wextra -o "$OUT" test_core.cpp save_core.cpp
"$OUT" "../../Test Results " "../../AppData"
