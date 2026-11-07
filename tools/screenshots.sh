#!/usr/bin/env bash
# Erzeugt alle Oberflaechen-Bilder neu und legt sie an den bekannten Stellen im Repository ab:
#   screenshots/controller_320x240/     RockBase-Controller, Host-Vorschau (tools/ui_preview/render.sh)
#   screenshots/controller-round_240x240/ Round Controller, Host-Vorschau (controller-round/tools/preview/render.sh)
#   screenshots/simulator/              Fensteraufnahmen des Desktop-Simulators (simulator/screenshots/*.sim)
# Alle Bilder entstehen aus dem echten UI-Code mit nachgebildeten bzw. simulierten Daten, nicht am Geraet.
# Aufruf: tools/screenshots.sh   (Voraussetzungen: git, cmake, gcc/g++, SDL2-Entwicklungsdateien, Python mit Pillow)
# Wird von .github/workflows/screenshots.yml nach Aenderungen auf main ausgefuehrt.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SHOTS="$ROOT/screenshots"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# LVGL und Theme fuer die Host-Vorschauen in die (nicht versionierten) third_party-Ordner holen,
# Versionen aus den cmake/third_party.cmake der Projekte (nicht doppelt gepflegt)
fetch() {
  cat > "$WORK/fetch.cmake" <<CM
cmake_minimum_required(VERSION 3.16)
include("$1/cmake/third_party.cmake")
levi_git_tag(lvgl \${LEVI_LVGL_URL} \${LEVI_LVGL_REF})
levi_git_tag(lvgl_theme_shadcn \${LEVI_THEME_URL} \${LEVI_THEME_REF})
CM
  cmake -P "$WORK/fetch.cmake"
}
fetch "$ROOT/controller"
fetch "$ROOT/controller-round"

echo "== RockBase-Controller (Host-Vorschau)"
"$ROOT/tools/ui_preview/render.sh" "$SHOTS/controller_320x240"

echo "== Round Controller (Host-Vorschau)"
"$ROOT/controller-round/tools/preview/render.sh" "$SHOTS/controller-round_240x240"

echo "== Desktop-Simulator"
SIMB="${LEVI_SIM_BUILD_DIR:-$ROOT/simulator/build-screenshots}"
cmake -S "$ROOT/simulator" -B "$SIMB" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$SIMB" -j"$(nproc)" --target leviluma-sim-controller leviluma-sim-round
mkdir -p "$WORK/sim"
for v in controller round; do
  (cd "$WORK/sim" && SDL_VIDEODRIVER=offscreen "$SIMB/bin/leviluma-sim-$v" --no-dim \
     --script "$ROOT/simulator/screenshots/$v.sim")
done
mkdir -p "$SHOTS/simulator"
python3 - "$WORK/sim" "$SHOTS/simulator" <<'PY'
import glob, os, sys
from PIL import Image
files = sorted(glob.glob(os.path.join(sys.argv[1], "*.bmp")))
if not files:
    sys.exit("keine Simulator-Bilder erzeugt")
for f in files:
    n = os.path.splitext(os.path.basename(f))[0] + ".png"
    Image.open(f).convert("RGB").save(os.path.join(sys.argv[2], n))
    print("geschrieben:", n)
PY
