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
#   firmware/tests/syntax_check.sh                 both displays (display_type oled, then tft)
#   DISPLAYS=tft firmware/tests/syntax_check.sh     one of them
#
# Errors inside ESPHome's own headers come from the stand-ins and are not shown; only errors in components/athan
# and in athan.yaml lambdas are reported. Two known stand-in artefacts are filtered (see KNOWN below).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
FW="$(cd "$HERE/.." && pwd)"
ESPHOME="${ESPHOME:-esphome}"
CXX="${CXX:-clang++}"
WORK="${TMPDIR:-/tmp}/athan_syntax_check"
# ESPHome writes its build next to the yaml unless ESPHOME_DATA_DIR points elsewhere (build_firmware.sh points it at
# the owner's build cache). The generated code must land in the temporary copy, never in a real build folder.
unset ESPHOME_DATA_DIR

rm -rf "$WORK"
mkdir -p "$WORK"
cp -R "$FW" "$WORK/fw"
rm -rf "$WORK/fw/.esphome"
# Placeholder tones when the real ones are not in firmware/sounds/ yet (10 ms of silence, mono 16-bit 44.1 kHz WAV).
for t in click volume; do
  if [ ! -f "$WORK/fw/sounds/$t.wav" ]; then
    python3 -c "import sys, wave; w = wave.open(sys.argv[1], 'wb'); w.setnchannels(1); w.setsampwidth(2); \
w.setframerate(44100); w.writeframes(bytes(882)); w.close()" "$WORK/fw/sounds/$t.wav"
  fi
done

STUBS="$WORK/stubs"
cp -R "$HERE/idf_stubs" "$STUBS"
curl -sfL -o "$STUBS/ArduinoJson.h" \
  "https://github.com/bblanchon/ArduinoJson/releases/download/v7.4.3/ArduinoJson-v7.4.3.h" \
  || { echo "== could not download ArduinoJson.h (no network?)"; exit 1; }

DEFS=(-DUSE_ESP32 -DUSE_ESP32_FRAMEWORK_ESP_IDF -DUSE_ESP32_VARIANT_ESP32S3 -DUSE_ESP_IDF -DUSE_LWIP_FAST_SELECT
      -DESPHOME_LOG_LEVEL=ESPHOME_LOG_LEVEL_DEBUG)
B=""  # the generated sources, found after each generation

fail=0
speaker_checked=0
# A header with no stand-in is a fatal error: clang stops there and checks nothing after it, so every lambda would
# "pass". Report it instead (add the missing declarations to tests/idf_stubs/). A source file that is not there is
# reported too: clang then checks nothing at all.
fatal() {
  local f
  f=$(echo "$1" | grep "fatal error" | head -3 || true)
  if [ -n "$f" ]; then
    echo "$f" | sed "s|$B/||; s|^|   missing stand-in: |"
    return 0
  fi
  f=$(echo "$1" | grep -E "error: (no such file or directory|no input files)" | head -3 || true)
  [ -z "$f" ] && return 1
  echo "$f" | sed "s|^|   not checked: |"
}
for DISP in ${DISPLAYS:-oled tft}; do
  echo "======== display_type: $DISP"
  echo "== generating C++ (esphome compile --only-generate)"
  rm -rf "$WORK/fw/.esphome"
  (cd "$WORK/fw" && "$ESPHOME" -s display_type "$DISP" compile --only-generate athan.yaml > "$WORK/generate.log" 2>&1) || {
    tail -30 "$WORK/generate.log"
    exit 1
  }
  # ESPHome writes .esphome/build/<the yaml's name>/src (device_name decides the name): find it rather than assume
  # athan, and stop if there is nothing, or nothing would be checked and every step would print "ok".
  MAIN="$(find "$WORK/fw/.esphome/build" -mindepth 3 -maxdepth 3 -path '*/src/main.cpp' 2>/dev/null || true)"
  if [ -z "$MAIN" ] || [ "$(echo "$MAIN" | wc -l | tr -d ' ')" != 1 ]; then
    echo "== expected one generated main.cpp under $WORK/fw/.esphome/build, found: ${MAIN:-none} (see $WORK/generate.log)"
    exit 1
  fi
  B="$(dirname "$MAIN")"
  FLAGS=(-std=gnu++20 "${DEFS[@]}" -I"$B" -I"$STUBS" -fsyntax-only -ferror-limit=0)
  srcs=("$B"/esphome/components/athan/*.cpp)
  if [ ! -f "${srcs[0]}" ]; then
    echo "== no components/athan sources in $B: nothing was checked"
    exit 1
  fi

  echo "== components/athan"
  for p in "${srcs[@]}"; do
    f=$(basename "$p" .cpp)
    all=$("$CXX" "${FLAGS[@]}" -Wall -Wextra -Wno-unused-parameter "$p" 2>&1 || true)
    out=$(echo "$all" | grep -E "components/athan/[^:]+:[0-9]+:[0-9]+: (error|warning)" || true)
    if [ -n "$out" ]; then echo "$out" | sed "s|$B/||"; fail=1;
    elif fatal "$all"; then fail=1; else echo "   $f.cpp ok"; fi
  done

  if [ "$speaker_checked" = 0 ]; then
    speaker_checked=1
    echo "== components/speaker (ESPHome's own, with the ATHAN PATCH blocks: components/speaker/README.md)"
    PY="$(dirname "$(command -v "$ESPHOME")")/python"
    [ -x "$PY" ] || PY=python3
    PKG=$("$PY" -c "import esphome, os; print(os.path.dirname(esphome.__file__))")
    "$PY" "$HERE/check_speaker_override.py" "$PKG" || fail=1
    spk=("$B"/esphome/components/speaker/media_player/*.cpp)
    if [ ! -f "${spk[0]}" ]; then
      echo "   no components/speaker/media_player sources in $B: not checked"
      fail=1
    fi
    for p in "${spk[@]}"; do
      [ -f "$p" ] || continue
      f=$(basename "$p" .cpp)
      out=$("$CXX" "${FLAGS[@]}" -Wno-unused-parameter "$p" 2>&1 \
            | grep -E "components/speaker/media_player/[^:]+:[0-9]+:[0-9]+: error" || true)
      if [ -n "$out" ]; then echo "$out" | sed "s|$B/||"; fail=1; else echo "   media_player/$f.cpp ok"; fi
    done
  fi

  echo "== athan.yaml and displays/$DISP.yaml lambdas (main.cpp)"
  # Stand-in artefacts: the mixer/resampler headers need audio libraries that are not stubbed, and the Bluetooth
  # headers need large ESP-IDF unions. Both are ESPHome's own generated wiring, not lambdas.
  KNOWN='SourceSpeaker|esp_gatt|esp_ble|BLEServer|ESP32BLE|I2CBus'
  all=$("$CXX" "${FLAGS[@]}" "$B/main.cpp" 2>&1 || true)
  out=$(echo "$all" | grep -E "(main\.cpp|\.yaml):[0-9]+:[0-9]+: error" | grep -v -E "$KNOWN" || true)
  if [ -n "$out" ]; then echo "$out" | sed "s|$B/||; s|$WORK/fw/||"; fail=1;
  elif fatal "$all"; then fail=1; else echo "   all lambdas ok"; fi
done

if [ "$fail" = 0 ]; then echo "== OK (type check only: the real build can still fail on linking or ESP-IDF details)"; fi
exit $fail
