#!/usr/bin/env bash
# Baut und startet die Host-Tests (gcc/g++ mit AddressSanitizer und UBSan).
# Aufruf: run_tests.sh [all|build|run]   (Standard all). Ergebnisdatei fuer den Bericht: LEVI_TEST_RESULTS=<Datei>.
# Lokale Laeufe aendern tests/README.md nicht; die Auswertung entsteht nur im abschliessenden CI-Lauf.
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
MODE="${1:-all}"
if [ "$MODE" = all ] || [ "$MODE" = build ]; then
  cmake -S "$DIR" -B "$DIR/build" -G Ninja > /dev/null 2>&1 || cmake -S "$DIR" -B "$DIR/build" > /dev/null
  cmake --build "$DIR/build"
fi
if [ "$MODE" = all ] || [ "$MODE" = run ]; then
  "$DIR/build/levi_tests"
fi
