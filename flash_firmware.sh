#!/usr/bin/env bash
# Flash firmware that is already built (firmware/binaries/), without building it again.
#
#   ./flash_firmware.sh ota [HOST]        over Wi-Fi (default: athan.local). Keeps settings, Wi-Fi, sounds and
#                                         prayer times
#   ./flash_firmware.sh usb [PORT]        over a serial port, the firmware only. Keeps settings, Wi-Fi, sounds and prayer
#                                         times. For a board that already runs version 3
#   ./flash_firmware.sh usb-full [PORT]   over a serial port, the whole image from 0x0 (bootloader + partition table +
#                                         firmware). For a blank board, or after partitions.csv changed. Resets the
#                                         settings and the saved Wi-Fi (sounds and prayer times stay)
#
# Options:  -f FILE   flash another file, for example athan-v3.ota.bin downloaded from a GitHub Release
#           -y        don't ask before usb-full
#
# Default files (written by ./build_firmware.sh): ota and usb send firmware/binaries/athan-v3.ota.bin, usb-full
# the newest firmware/binaries/athan-v3-*.factory.bin. Without PORT, the USB port is found by itself (asks if
# there are several). On the V3 board that port is a 3.3 V USB-serial adapter on J6 (GND 1, adapter RX 4, TX 5):
# put the jumper on J6 pins 1-2 before powering the board, and remove it afterwards.
#
# Safety: every file is checked to be an ESP32-S3 image of the right kind before anything is sent. Wi-Fi updates
# go only to a clock that serves the version 3 /audio page: a V2 clock also answers to athan.local and an ESP32
# image would brick it. USB writes use --chip esp32s3, so esptool refuses any other chip.
#
# Needs: curl (Wi-Fi); esptool for USB (taken from build_firmware.sh's ESPHome environment, or installed alone
# into the same cache folder on first use).
set -euo pipefail

# Shared settings and helpers: cache folder, Python, say/die, device_name, ensure_v3_device.
# shellcheck source=build_firmware.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build_firmware.sh"

ESPTOOL=""

ensure_esptool() {
  if [ -x "$VENV/bin/esptool" ]; then
    ESPTOOL="$VENV/bin/esptool"
    return 0
  fi
  local ev="$CACHE/esptool" py
  if [ ! -f "$ev/.installed" ]; then
    py="$(find_python || command -v python3 || true)"
    [ -n "$py" ] || die "esptool needs Python 3 (python.org or brew install python@3.13)."
    say "Installing esptool into $ev (once)"
    rm -rf "$ev"
    mkdir -p "$CACHE"
    "$py" -m venv "$ev"
    "$ev/bin/python" -m pip install --quiet --upgrade pip
    "$ev/bin/python" -m pip install --quiet esptool
    touch "$ev/.installed"
  fi
  ESPTOOL="$ev/bin/esptool"
}

# check_image FILE app|factory: an ESP32-S3 image of that kind, or stop.
check_image() {
  local file="$1" kind="$2"
  [ -f "$file" ] || die "$file not found. Build it with ./build_firmware.sh, or pass another file with -f."
  python3 - "$file" "$kind" <<'EOF' || exit 1
import sys
path, kind = sys.argv[1], sys.argv[2]
data = open(path, "rb").read()
ESP32S3 = 9  # esp_chip_id_t in the image header (bytes 12-13)

def chip(off):
    if len(data) < off + 24 or data[off] != 0xE9:
        return None
    return int.from_bytes(data[off + 12:off + 14], "little")

# A factory image carries the partition table at 0x8000 (entries start with 0xAA 0x50) and the app at 0x10000.
is_factory = len(data) > 0x10000 and data[0x8000:0x8002] == b"\xaa\x50"
if kind == "app":
    if is_factory:
        sys.exit(f"{path} is a full factory image; flash it with: usb-full")
    if chip(0) != ESP32S3:
        sys.exit(f"{path} is not an ESP32-S3 firmware image (V2 ESP8266 file, or not firmware at all); nothing sent")
else:
    if not is_factory or chip(0) != ESP32S3 or chip(0x10000) != ESP32S3:
        sys.exit(f"{path} is not an ESP32-S3 factory image (use the athan-v3-*.factory.bin from a build); nothing sent")
EOF
}

# pick_port [PORT]: the given port, the only USB serial port, or a choice between several.
pick_port() {
  if [ -n "${1:-}" ]; then
    echo "$1"
    return 0
  fi
  local ports=() p
  for p in /dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART* /dev/ttyACM* /dev/ttyUSB*; do
    [ -e "$p" ] && ports+=("$p")
  done
  if [ ${#ports[@]} -eq 0 ]; then
    die "no serial port found. V3 board: plug the USB-serial adapter into J6 (jumper J6 1-2 at power-up). DevKitC: use a data USB cable (some only charge)."
  elif [ ${#ports[@]} -eq 1 ]; then
    echo "${ports[0]}"
  else
    echo "Several serial ports found:" >&2
    PS3="Which one is the clock? "
    select p in "${ports[@]}"; do
      if [ -n "$p" ]; then
        echo "$p"
        return 0
      fi
    done
    die "no port chosen"
  fi
}

# partition FIELD NAME: offset or size of a partition in firmware/partitions.csv.
partition() {
  awk -F, -v want="$2" -v col="$([ "$1" = size ] && echo 5 || echo 4)" '
    /^[[:space:]]*#/ { next }
    { name = $1; gsub(/[[:space:]]/, "", name); if (name == want) { v = $col; gsub(/[[:space:]]/, "", v); print v; exit } }
  ' "$ROOT/firmware/partitions.csv"
}

newest_factory() {
  ls -t "$OUT"/athan-v3-*.factory.bin 2>/dev/null | head -1 || true
}

flash_ota() {
  local file="$1" host="$2" resp
  check_image "$file" app
  ensure_v3_device "$host"
  say "Sending $(basename "$file") to $host over Wi-Fi"
  resp="$(curl --fail --show-error --progress-bar --max-time 600 -F "update=@$file" "http://$host/update")" \
    || die "upload failed (connection lost?). The clock keeps its current firmware."
  case "$resp" in
    *"Update Successful"*) say "Done. $host restarts with the new firmware in a few seconds." ;;
    *) die "the clock answered \"${resp:-nothing}\", so it keeps its current firmware." ;;
  esac
}

flash_usb_app() {
  local file="$1" port otadata otasize app0 blank
  check_image "$file" app
  ensure_esptool
  port="$(pick_port "${2:-}")"
  otadata="$(partition offset otadata)"
  otasize="$(partition size otadata)"
  app0="$(partition offset app0)"
  [ -n "$otadata" ] && [ -n "$otasize" ] && [ -n "$app0" ] || die "could not read otadata/app0 from firmware/partitions.csv"
  # A blank otadata makes the bootloader start app0, where the new firmware goes (after OTA updates the clock may
  # have been running from app1).
  blank="$CACHE/otadata-blank.bin"
  mkdir -p "$CACHE"
  python3 -c "import sys; open(sys.argv[1], 'wb').write(b'\xff' * int(sys.argv[2], 0))" "$blank" "$otasize"
  say "Flashing $(basename "$file") over USB ($port): firmware only, settings kept"
  "$ESPTOOL" --chip esp32s3 --port "$port" write-flash "$otadata" "$blank" "$app0" "$file"
  say "Done. On the V3 board: remove the J6 jumper, then unplug and replug the USB-C (a DevKitC restarts by itself)."
}

flash_usb_full() {
  local file="$1" port answer
  check_image "$file" factory
  if [ "$ASSUME_YES" != 1 ]; then
    printf 'usb-full writes the whole image and RESETS the settings and the saved Wi-Fi (sounds and prayer times stay). Continue? [y/N] '
    read -r answer
    case "$answer" in y|Y|yes|YES) ;; *) die "cancelled" ;; esac
  fi
  ensure_esptool
  port="$(pick_port "${2:-}")"
  say "Flashing $(basename "$file") over USB ($port): full image from 0x0"
  "$ESPTOOL" --chip esp32s3 --port "$port" write-flash 0x0 "$file"
  say "Done. On the V3 board: remove the J6 jumper and power-cycle it; then set up its Wi-Fi again (README 1.1)."
}

flash_main() {
  local cmd="" target="" file="" args=()
  ASSUME_YES=0
  while [ $# -gt 0 ]; do
    case "$1" in
      -f|--file) [ $# -ge 2 ] || die "-f needs a file"; file="$2"; shift 2 ;;
      -y|--yes) ASSUME_YES=1; shift ;;
      -h|--help|help) sed -n '2,/^set -euo/p' "${BASH_SOURCE[0]}" | sed '$d' | sed 's/^# \{0,1\}//'; return 0 ;;
      -*) die "unknown option $1" ;;
      *) args+=("$1"); shift ;;
    esac
  done
  [ ${#args[@]} -ge 1 ] || die "say how to flash: ota, usb or usb-full (./flash_firmware.sh help)"
  cmd="${args[0]}"
  [ ${#args[@]} -ge 2 ] && target="${args[1]}"
  case "$cmd" in
    ota) flash_ota "${file:-$OUT/athan-v3.ota.bin}" "${target:-$(device_name).local}" ;;
    usb) flash_usb_app "${file:-$OUT/athan-v3.ota.bin}" "$target" ;;
    usb-full)
      file="${file:-$(newest_factory)}"
      [ -n "$file" ] || die "no firmware/binaries/athan-v3-*.factory.bin yet. Build it with ./build_firmware.sh, or pass one with -f."
      flash_usb_full "$file" "$target"
      ;;
    *) die "unknown command '$cmd' (ota, usb, usb-full, help)" ;;
  esac
}

flash_main "$@"
