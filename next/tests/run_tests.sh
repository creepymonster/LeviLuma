#!/usr/bin/env bash
# Baut und startet die Host-Tests (gcc/g++ mit AddressSanitizer und UBSan).
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
cmake -S "$DIR" -B "$DIR/build" -G Ninja > /dev/null 2>&1 || cmake -S "$DIR" -B "$DIR/build" > /dev/null
cmake --build "$DIR/build"
"$DIR/build/levi_tests"
