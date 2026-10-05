#!/usr/bin/env bash
# Build and flash the athan firmware with ESPHome, without Docker or the ESPHome dashboard.
#
# The first run installs a pinned ESPHome into its own Python environment and downloads the ESP32 toolchain
# (about 1 GB, once). Everything it downloads or builds lives in a cache folder outside this repository
# (default ~/.cache/esp32_athan), so the repository stays clean.
#
#   ./build_firmware.sh                 build; binaries go to firmware/binaries/
#   ./build_firmware.sh flash [PORT]    build, then flash over USB-C (first flash: also writes the partition table)
#   ./build_firmware.sh ota [HOST]      build, then update over Wi-Fi (default: athan.local)
#   ./build_firmware.sh logs [PORT|HOST]  show the device's log
#   ./build_firmware.sh check           validate firmware/athan.yaml only (fast, no compile)
#   ./build_firmware.sh clean [all]     delete the build folder (all: also ESPHome and the toolchain)
#
# Without PORT, ESPHome lists the serial ports it finds and asks. On macOS the board's USB port looks like
# /dev/cu.usbmodem1101.
#
# firmware/binaries/ after a build:
#   athan-v3-<version>.factory.bin  full image for a blank board (any ESP web flasher / esptool at offset 0x0)
#   athan-v3.ota.bin + manifest.json   the two assets of a GitHub Release (README section 3.4)
#
# Settings (environment variables):
#   ESPHOME_VERSION   ESPHome to use (default 2026.9.1, the version the firmware was checked against)
#   ATHAN_CACHE       cache folder (default ~/.cache/esp32_athan)
#   PYTHON            Python 3.12-3.14 to create the environment with (default: the first one found)
#
# Needs: Python 3.12, 3.13 or 3.14 and internet on the first run. Before the first build, put click.mp3 and
# volume.mp3 into firmware/sounds/ (README section 2.2).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
YAML="$ROOT/firmware/athan.yaml"
OUT="$ROOT/firmware/binaries"
ESPHOME_VERSION="${ESPHOME_VERSION:-2026.9.1}"
CACHE="${ATHAN_CACHE:-$HOME/.cache/esp32_athan}"
VENV="$CACHE/esphome-$ESPHOME_VERSION"

say() { printf '\033[1m==> %s\033[0m\n' "$*"; }
die() { printf '\033[31mError: %s\033[0m\n' "$*" >&2; exit 1; }

# ESPHome keeps its build folder (.esphome) next to the yaml and writes a .gitignore there unless told otherwise.
# Redirect both, and keep PlatformIO's toolchains in the cache too.
export ESPHOME_DATA_DIR="$CACHE/data"
export ESPHOME_NOGITIGNORE=1
export PLATFORMIO_CORE_DIR="$CACHE/platformio"
# Python would otherwise drop __pycache__ into firmware/components/athan/ when ESPHome loads the component.
export PYTHONPYCACHEPREFIX="$CACHE/pycache"

find_python() {
  local candidates=("${PYTHON:-}" python3.13 python3.12 python3 python3.14)
  local py
  for py in "${candidates[@]}"; do
    [ -n "$py" ] || continue
    command -v "$py" >/dev/null 2>&1 || continue
    if "$py" -c 'import sys; sys.exit(0 if (3, 12) <= sys.version_info[:2] < (3, 15) else 1)' 2>/dev/null; then
      echo "$py"
      return 0
    fi
  done
  return 1
}

ensure_esphome() {
  if [ -f "$VENV/.installed" ]; then
    return 0
  fi
  local py
  py="$(find_python)" || die "ESPHome $ESPHOME_VERSION needs Python 3.12, 3.13 or 3.14. Install one (python.org or brew install python@3.13), or set PYTHON=/path/to/python3."
  say "Installing ESPHome $ESPHOME_VERSION into $VENV (once, with $("$py" --version))"
  rm -rf "$VENV"
  mkdir -p "$CACHE"
  "$py" -m venv "$VENV"
  "$VENV/bin/python" -m pip install --quiet --upgrade pip
  "$VENV/bin/python" -m pip install --quiet "esphome==$ESPHOME_VERSION"
  touch "$VENV/.installed"
}

esphome() { "$VENV/bin/esphome" "$@"; }

check_sounds() {
  local missing=() t
  for t in click volume; do
    [ -f "$ROOT/firmware/sounds/$t.mp3" ] || missing+=("firmware/sounds/$t.mp3")
  done
  if [ ${#missing[@]} -gt 0 ]; then
    die "missing ${missing[*]}. These are the menu click and the volume tone built into the firmware (V2 SD files C3 and C2, or any short MP3). See README section 2.2."
  fi
}

project_version() {
  sed -n 's/^[[:space:]]*project_version:[[:space:]]*"\{0,1\}\([^"#[:space:]]*\)"\{0,1\}.*/\1/p' "$YAML" | head -1
}

# Newest build output named $1 (firmware.ota.bin or firmware.factory.bin) under the build folder. Works for both
# ESPHome toolchains (PlatformIO: build/<name>/.pioenvs/<name>/, native ESP-IDF: build/<name>/build/).
newest_output() {
  "$VENV/bin/python" - "$ESPHOME_DATA_DIR/build" "$1" <<'EOF'
import pathlib, sys
files = [p for p in pathlib.Path(sys.argv[1]).rglob(sys.argv[2]) if p.is_file()]
if files:
    print(max(files, key=lambda p: p.stat().st_mtime))
EOF
}

collect_binaries() {
  local version ota factory
  version="$(project_version)"
  [ -n "$version" ] || die "could not read project_version from $YAML"
  ota="$(newest_output firmware.ota.bin)"
  factory="$(newest_output firmware.factory.bin)"
  [ -n "$ota" ] || die "the build finished but no firmware.ota.bin was found under $ESPHOME_DATA_DIR/build"
  mkdir -p "$OUT"
  if [ -n "$factory" ]; then
    cp "$factory" "$OUT/athan-v3-$version.factory.bin"
  fi
  cp "$ota" "$OUT/athan-v3.ota.bin"
  # Writes manifest.json next to it (the GitHub Release pair, README section 3.4).
  "$VENV/bin/python" "$ROOT/scripts/make_release_manifest.py" "$OUT/athan-v3.ota.bin" --version "$version" >/dev/null
  say "Firmware $version built:"
  ls -l "$OUT" | sed -n '2,$p' | sed 's/^/    /'
}

build() {
  check_sounds
  ensure_esphome
  say "Compiling firmware/athan.yaml with ESPHome $ESPHOME_VERSION (the first build takes a while)"
  esphome compile "$YAML"
  collect_binaries
}

check() {
  check_sounds
  ensure_esphome
  local log="$CACHE/check.log"
  if esphome config "$YAML" >"$log" 2>&1; then
    grep -E '^WARNING' "$log" || true
    say "firmware/athan.yaml is valid (ESPHome $ESPHOME_VERSION)"
  else
    tail -40 "$log"
    die "firmware/athan.yaml is not valid (full output: $log)"
  fi
}

main() {
  local cmd="${1:-build}"
  case "$cmd" in
    build)
      build
      ;;
    flash)
      build
      say "Flashing over USB${2:+ ($2)}"
      esphome upload "$YAML" ${2:+--device "$2"}
      ;;
    ota)
      build
      say "Updating over Wi-Fi (${2:-athan.local})"
      esphome upload "$YAML" --device "${2:-OTA}"
      ;;
    logs)
      ensure_esphome
      esphome logs "$YAML" ${2:+--device "$2"}
      ;;
    check)
      check
      ;;
    clean)
      if [ "${2:-}" = "all" ]; then
        say "Deleting $CACHE (ESPHome, toolchain and build)"
        rm -rf "$CACHE"
      else
        say "Deleting the build folder $ESPHOME_DATA_DIR"
        rm -rf "$ESPHOME_DATA_DIR"
      fi
      ;;
    -h|--help|help)
      sed -n '2,/^set -euo/p' "${BASH_SOURCE[0]}" | sed '$d' | sed 's/^# \{0,1\}//'
      ;;
    *)
      die "unknown command '$cmd' (try: build, flash, ota, logs, check, clean, help)"
      ;;
  esac
}

# Run only when executed, so the functions can be sourced for testing.
if [ "${BASH_SOURCE[0]}" = "$0" ]; then
  main "$@"
fi
