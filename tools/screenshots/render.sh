#!/usr/bin/env bash
# Rendert die LCD-UI aus src/main.cpp in feste PNG-Dateien, je Aufloesung in einem Unterordner.
# Aufruf: tools/screenshots/render.sh <repo-root> <lvgl-quellordner> <theme-quellordner> <ausgabe-wurzel>
# Ergebnis: <ausgabe-wurzel>/<B>x<H>/*.png
set -euo pipefail
REPO="$(cd "$1" && pwd)"; LVGL="$(cd "$2" && pwd)"; THEME="$(cd "$3" && pwd)"; OUT_ROOT="$4"
BUILD="$(mktemp -d)"

# Aufloesung aus main.cpp: Unterordner je Display, z. B. screenshots/240x240/
res_w=$(grep -o 'static constexpr uint16_t W=[0-9]*' "$REPO/src/main.cpp" | grep -o '[0-9]*$')
res_h=$(grep -o 'H=[0-9]*' "$REPO/src/main.cpp" | head -1 | grep -o '[0-9]*$')
OUT="$OUT_ROOT/${res_w}x${res_h}"
mkdir -p "$OUT"

# UI-Abschnitt aus main.cpp: von den Zustandsvariablen bis vor wlStatusName (Flush, Touch, UI, Farben)
start=$(grep -n '^static constexpr uint16_t W=240' "$REPO/src/main.cpp" | head -1 | cut -d: -f1)
end=$(grep -n '^static const char \*wlStatusName' "$REPO/src/main.cpp" | head -1 | cut -d: -f1)
sed -n "${start},$((end-1))p" "$REPO/src/main.cpp" > "$BUILD/ui_part.inc"

# LVGL aus dem Quellcode bauen (eigene lv_conf.h aus include/)
find "$LVGL/src" -name '*.c' | xargs -P "$(nproc)" -I{} sh -c \
  'gcc -std=gnu99 -O1 -DLV_CONF_INCLUDE_SIMPLE -I "$0" -I "$1" -w -c "$2" -o "$3/$(echo "$2" | tr / _).o"' \
  "$LVGL" "$REPO/include" {} "$BUILD"
find "$THEME/src" -name '*.c' | xargs -P "$(nproc)" -I{} sh -c \
  'gcc -std=gnu99 -O1 -DLV_CONF_INCLUDE_SIMPLE -I "$0" -I "$1" -I "$2" -w -c "$3" -o "$4/theme_$(echo "$3" | tr / _).o"' \
  "$LVGL" "$REPO/include" "$THEME/include" {} "$BUILD"

g++ -std=c++17 -O1 -DLV_CONF_INCLUDE_SIMPLE -I "$LVGL" -I "$REPO/include" -I "$THEME/include" -I "$BUILD" -w \
  "$REPO/tools/screenshots/harness.cpp" "$BUILD"/*.o -o "$BUILD/shot" -lm
mkdir -p "$BUILD/ppm"
"$BUILD/shot" "$BUILD/ppm"

python3 - "$BUILD/ppm" "$OUT" <<'PY'
import glob, os, sys
from PIL import Image
src, out = sys.argv[1], sys.argv[2]
for f in sorted(glob.glob(os.path.join(src, "*.ppm"))):
    name = os.path.splitext(os.path.basename(f))[0]
    Image.open(f).convert("RGB").save(os.path.join(out, name + ".png"))
    print("geschrieben:", name + ".png")
PY
rm -rf "$BUILD"
