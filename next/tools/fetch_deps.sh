#!/usr/bin/env bash
# Holt die externen Komponenten des Controllers vorab (sonst passiert das beim ersten CMake-Lauf).
# Aufruf: next/tools/fetch_deps.sh [--zigbee]
set -euo pipefail
DIR="$(cd "$(dirname "$0")/../controller" && pwd)"
ZIGBEE=OFF
[[ "${1:-}" == "--zigbee" ]] && ZIGBEE=ON
TMP="$(mktemp)"
trap 'rm -f "$TMP"' EXIT
cat > "$TMP" <<CM
cmake_minimum_required(VERSION 3.16)
include("$DIR/cmake/third_party.cmake")
levi_git_tag(lvgl \${LEVI_LVGL_URL} \${LEVI_LVGL_REF})
levi_git_tag(lvgl_theme_shadcn \${LEVI_THEME_URL} \${LEVI_THEME_REF})
if($ZIGBEE)
  levi_git_zigbee()
endif()
CM
cmake -P "$TMP"
echo "Abhaengigkeiten liegen in $DIR/third_party"
