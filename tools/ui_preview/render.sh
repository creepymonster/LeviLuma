#!/usr/bin/env bash
# Host-Vorschau der Controller-Oberflaeche (320x240) als PNG.
# Aufruf: tools/ui_preview/render.sh <ausgabeordner>
# Voraussetzung: LVGL und Theme in controller/third_party (tools/fetch_deps.sh), gcc, Pillow.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; NEXT="$(cd "$HERE/../.." && pwd)"; OUT="$(mkdir -p "$1" && cd "$1" && pwd)"
TP="$NEXT/controller/third_party"; B="$(mktemp -d)"
find "$TP/lvgl/src" -name '*.c' | xargs -P "$(nproc)" -I{} sh -c 'gcc -std=gnu99 -O1 -DLV_CONF_INCLUDE_SIMPLE -I "$0" -I "$1" -w -c "$2" -o "$3/$(echo "$2" | tr / _).o"' "$TP/lvgl" "$HERE" {} "$B"
find "$TP/lvgl_theme_shadcn/src" -name '*.c' | xargs -P "$(nproc)" -I{} sh -c 'gcc -std=gnu99 -O1 -DLV_CONF_INCLUDE_SIMPLE -I "$0" -I "$1" -I "$2" -w -c "$3" -o "$4/t_$(echo "$3" | tr / _).o"' "$TP/lvgl" "$HERE" "$TP/lvgl_theme_shadcn/include" {} "$B"
INC=(-I "$HERE/stub" -I "$HERE" -I "$TP/lvgl" -I "$TP/lvgl_theme_shadcn/include" -I "$NEXT/controller/main" -I "$NEXT/controller/components/controller_core/include" -I "$NEXT/shared/protocol/include" -I "$NEXT/shared/common/include")
g++ -std=c++20 -O1 -DLV_CONF_INCLUDE_SIMPLE -DCONFIG_LEVILUMA_HOME_MS=60000 -DCONFIG_LEVILUMA_ESPNOW_CHANNEL=1 -DCONFIG_LEVILUMA_TOUCH_CAL_ON_FIRST_BOOT=0 \
  -Wall -Wno-unused-function "${INC[@]}" "$HERE/harness.cpp" \
  "$NEXT"/controller/main/ui_{common,home,detail,settings,calib,touchcal}.cpp \
  "$NEXT/controller/components/controller_core/src/touch_cal.cpp" "$NEXT/shared/protocol/src/leviluma_proto.c" "$NEXT/shared/common/src/levi_key.c" "$B"/*.o -o "$B/preview" -lm
"$B/preview" "$B"
python3 - "$B" "$OUT" <<'PY'
import glob, os, sys
from PIL import Image
for f in sorted(glob.glob(os.path.join(sys.argv[1], "*.ppm"))):
    n = os.path.splitext(os.path.basename(f))[0]
    Image.open(f).convert("RGB").save(os.path.join(sys.argv[2], n + ".png")); print("geschrieben:", n + ".png")
PY
rm -rf "$B"
