#!/bin/bash
# Type-check the custom component (components/athan) and every yaml lambda against the REAL ESPHome headers,
# on a computer, without the ESP32 toolchain or a firmware build.
#
#   1. `esphome compile --only-generate` on a temporary copy (writes main.cpp; nothing is compiled)
#   2. clang++ -fsyntax-only on the component sources and on main.cpp, with small stand-ins for the
#      ESP-IDF headers (tests/idf_stubs/)
#
# Needs: ESPHome (same version as the build, e.g. `pip install esphome==2026.9.1` in a venv; set ESPHOME=/path/to/
# esphome if it is not on PATH), a C++20 clang++ (or CXX=g++), curl, python3. First run downloads Google Fonts and
# ArduinoJson.
#
#   firmware/tests/syntax_check.sh
#
# Errors inside ESPHome's own headers come from the stand-ins and are not shown; only errors in components/athan
# and in athan.yaml lambdas are reported. Two known stand-in artefacts are filtered (see KNOWN below).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
FW="$(cd "$HERE/.." && pwd)"
ESPHOME="${ESPHOME:-esphome}"
CXX="${CXX:-clang++}"
WORK="${TMPDIR:-/tmp}/athan_syntax_check"

rm -rf "$WORK"
mkdir -p "$WORK"
cp -R "$FW" "$WORK/fw"
rm -rf "$WORK/fw/.esphome"
# Placeholder tones when the real ones are not in firmware/sounds/ yet (a few silent MP3 frames).
for t in click volume; do
  if [ ! -f "$WORK/fw/sounds/$t.mp3" ]; then
    python3 -c "import sys; f=bytes([0xFF,0xFB,0x90,0x00])+bytes(413); open(sys.argv[1],'wb').write(f*20)" \
      "$WORK/fw/sounds/$t.mp3"
  fi
done

echo "== generating C++ (esphome compile --only-generate)"
(cd "$WORK/fw" && "$ESPHOME" compile --only-generate athan.yaml > "$WORK/generate.log" 2>&1) || {
  tail -30 "$WORK/generate.log"
  exit 1
}
B="$WORK/fw/.esphome/build/athan/src"
STUBS="$WORK/stubs"
cp -R "$HERE/idf_stubs" "$STUBS"
curl -sfL -o "$STUBS/ArduinoJson.h" \
  "https://github.com/bblanchon/ArduinoJson/releases/download/v7.4.3/ArduinoJson-v7.4.3.h"

DEFS=(-DUSE_ESP32 -DUSE_ESP32_FRAMEWORK_ESP_IDF -DUSE_ESP32_VARIANT_ESP32S3 -DUSE_ESP_IDF -DUSE_LWIP_FAST_SELECT
      -DESPHOME_LOG_LEVEL=ESPHOME_LOG_LEVEL_DEBUG)
FLAGS=(-std=gnu++20 "${DEFS[@]}" -I"$B" -I"$STUBS" -fsyntax-only -ferror-limit=0)

fail=0
echo "== components/athan"
for p in "$B"/esphome/components/athan/*.cpp; do
  f=$(basename "$p" .cpp)
  out=$("$CXX" "${FLAGS[@]}" -Wall -Wextra -Wno-unused-parameter "$p" 2>&1 \
        | grep -E "components/athan/[^:]+:[0-9]+:[0-9]+: (error|warning)" || true)
  if [ -n "$out" ]; then echo "$out" | sed "s|$B/||"; fail=1; else echo "   $f.cpp ok"; fi
done

echo "== components/speaker (ESPHome's own, with the ATHAN PATCH blocks: components/speaker/README.md)"
PY="$(dirname "$(command -v "$ESPHOME")")/python"
[ -x "$PY" ] || PY=python3
PKG=$("$PY" -c "import esphome, os; print(os.path.dirname(esphome.__file__))")
"$PY" "$HERE/check_speaker_override.py" "$PKG" || fail=1
for p in "$B"/esphome/components/speaker/media_player/*.cpp; do
  f=$(basename "$p" .cpp)
  out=$("$CXX" "${FLAGS[@]}" -Wno-unused-parameter "$p" 2>&1 \
        | grep -E "components/speaker/media_player/[^:]+:[0-9]+:[0-9]+: error" || true)
  if [ -n "$out" ]; then echo "$out" | sed "s|$B/||"; fail=1; else echo "   media_player/$f.cpp ok"; fi
done

echo "== athan.yaml lambdas (main.cpp)"
# Stand-in artefacts: the mixer/resampler headers need audio libraries that are not stubbed, and the Bluetooth
# headers need large ESP-IDF unions. Both are ESPHome's own generated wiring, not lambdas.
KNOWN='SourceSpeaker|esp_gatt|esp_ble|BLEServer|ESP32BLE|I2CBus'
out=$("$CXX" "${FLAGS[@]}" "$B/main.cpp" 2>&1 | grep -E "(main\.cpp|athan\.yaml):[0-9]+:[0-9]+: error" \
      | grep -v -E "$KNOWN" || true)
if [ -n "$out" ]; then echo "$out" | sed "s|$B/||; s|$WORK/fw/||"; fail=1; else echo "   all lambdas ok"; fi

if [ "$fail" = 0 ]; then echo "== OK (type check only: the real build can still fail on linking or ESP-IDF details)"; fi
exit $fail
