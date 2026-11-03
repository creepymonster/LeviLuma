#!/usr/bin/env bash
# Host-Vorschau der runden Oberflaeche (240x240) als PNG.
# Aufruf: next/controller-round/tools/preview/render.sh <ausgabeordner>
# Voraussetzung: LVGL und Theme in next/controller-round/third_party (einmal idf.py build oder
# cmake-Lauf), gcc, Pillow.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; R="$(cd "$HERE/../.." && pwd)"; NEXT="$(cd "$R/.." && pwd)"
OUT="$(mkdir -p "$1" && cd "$1" && pwd)"; TP="$R/third_party"; B="$(mktemp -d)"
find "$TP/lvgl/src" -name '*.c' | xargs -P "$(nproc)" -I{} sh -c 'gcc -std=gnu99 -O1 -DLV_CONF_INCLUDE_SIMPLE -I "$0" -I "$1" -w -c "$2" -o "$3/$(echo "$2" | tr / _).o"' "$TP/lvgl" "$HERE" {} "$B"
find "$TP/lvgl_theme_shadcn/src" -name '*.c' | xargs -P "$(nproc)" -I{} sh -c 'gcc -std=gnu99 -O1 -DLV_CONF_INCLUDE_SIMPLE -I "$0" -I "$1" -I "$2" -w -c "$3" -o "$4/t_$(echo "$3" | tr / _).o"' "$TP/lvgl" "$HERE" "$TP/lvgl_theme_shadcn/include" {} "$B"
g++ -std=c++20 -O0 -g -DLV_CONF_INCLUDE_SIMPLE -DCONFIG_LEVILUMA_HOME_MS=60000 -DCONFIG_LEVILUMA_INPUT_SEND_MS=120 -Wall \
  -I "$HERE/stub" -I "$HERE" -I "$TP/lvgl" -I "$TP/lvgl_theme_shadcn/include" -I "$R/main" -I "$R/components/round_core/include" \
  -I "$NEXT/controller/components/controller_core/include" -I "$NEXT/shared/protocol/include" -I "$NEXT/shared/common/include" \
  "$HERE/harness.cpp" "$R/main/ui.cpp" "$R/components/round_core/src/round_core.cpp" \
  "$NEXT/controller/components/controller_core/src/gateway_registry.cpp" "$NEXT/shared/common/src/kv_store.cpp" \
  "$NEXT/shared/common/src/levi_key.c" "$NEXT/shared/protocol/src/leviluma_proto.c" "$NEXT/shared/protocol/src/leviluma_link.c" \
  "$B"/*.o -o "$B/preview" -lm
"$B/preview" "$B" || { cp "$B/preview" /tmp/claude-0/-home-user-LeviLuma/2feb8cca-184f-5821-b8a2-0071a502b4b0/scratchpad/preview_bin 2>/dev/null; exit 1; }
python3 - "$B" "$OUT" <<'PY'
import glob, os, sys
from PIL import Image
for f in sorted(glob.glob(os.path.join(sys.argv[1], "*.ppm"))):
    n = os.path.splitext(os.path.basename(f))[0]
    Image.open(f).convert("RGB").save(os.path.join(sys.argv[2], n + ".png")); print("geschrieben:", n + ".png")
PY
rm -rf "$B"
