#!/usr/bin/env bash
# Rendert die LCD-UI aus src/main.cpp in feste PNG-Dateien.
# Aufruf: tools/screenshots/render.sh <repo-root> <lvgl-quellordner> <ausgabeordner>
set -euo pipefail
REPO="$(cd "$1" && pwd)"; LVGL="$(cd "$2" && pwd)"; OUT="$3"
BUILD="$(mktemp -d)"
mkdir -p "$OUT"

# UI-Abschnitt aus main.cpp: von den Zustandsvariablen bis vor wlStatusName (Flush, Touch, UI, Farben)
start=$(grep -n '^static constexpr uint16_t W=240' "$REPO/src/main.cpp" | head -1 | cut -d: -f1)
end=$(grep -n '^static const char \*wlStatusName' "$REPO/src/main.cpp" | head -1 | cut -d: -f1)
sed -n "${start},$((end-1))p" "$REPO/src/main.cpp" > "$BUILD/ui_part.inc"

# LVGL aus dem Quellcode bauen (eigene lv_conf.h aus include/)
find "$LVGL/src" -name '*.c' | xargs -P "$(nproc)" -I{} sh -c \
  'gcc -std=gnu99 -O1 -DLV_CONF_INCLUDE_SIMPLE -I "$0" -I "$1" -w -c "$2" -o "$3/$(echo "$2" | tr / _).o"' \
  "$LVGL" "$REPO/include" {} "$BUILD"

g++ -std=c++17 -O1 -DLV_CONF_INCLUDE_SIMPLE -I "$LVGL" -I "$REPO/include" -I "$BUILD" -w \
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
