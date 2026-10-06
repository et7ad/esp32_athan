#!/bin/bash
# Host unit tests (no ESP32 needed): firmware/partitions.csv, and components/athan/tz_posix.cpp + mp3_check.cpp.
# Needs: python3 (standard library), a C++17 compiler (c++/clang++/g++). Optional: ffprobe + some MP3 files.
#
#   firmware/tests/run_tests.sh                 # partition table, calendar, parser and time zone checks
#   firmware/tests/run_tests.sh a.mp3 b.mp3     # also check MP3 durations against ffprobe
set -euo pipefail
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}/athan_tests"
mkdir -p "$OUT"

# Partition table: what ESP-IDF's gen_esp32part.py checks at build time (overlaps, alignment, flash size), plus the
# sizes the athan component hard-codes (audio_slots.cpp regions, prayer_store.cpp slots).
python3 - ../partitions.csv ../components/athan/audio_slots.cpp ../components/athan/prayer_store.cpp <<'EOF'
import re, sys
csv, slots_cpp, store_cpp = sys.argv[1:4]
FLASH = 16 * 1024 * 1024
rows = []
for n, line in enumerate(open(csv), 1):
    line = line.split("#", 1)[0].strip()
    if not line:
        continue
    f = [x.strip() for x in line.split(",")]
    rows.append((n, f[0], f[1], int(f[3], 0), int(f[4], 0)))
errors = []
end = 0x9000  # partition table at 0x8000-0x9000
for n, name, typ, off, size in rows:
    align = 0x10000 if typ == "app" else 0x1000
    if off < end:
        errors.append(f"line {n} {name}: starts at {off:#x}, overlaps the previous partition (ends {end:#x})")
    if off % align or size % 0x1000:
        errors.append(f"line {n} {name}: offset {off:#x} / size {size:#x} not aligned to {align:#x}")
    end = off + size
if end > FLASH:
    errors.append(f"table ends at {end:#x}, beyond 16 MB")
byname = {r[1]: r for r in rows}
src = open(slots_cpp).read()
offs = [int(x, 0) for x in re.search(r"REGION_OFFSET\[[^]]*\]\s*=\s*\{([^}]*)\}", src).group(1).split(",")]
sizes = [int(x, 0) for x in re.search(r"REGION_SIZE\[[^]]*\]\s*=\s*\{([^}]*)\}", src).group(1).split(",")]
if offs[-1] + sizes[-1] > byname["audio"][4]:
    errors.append(f"audio regions end at {offs[-1] + sizes[-1]:#x}, beyond the audio partition ({byname['audio'][4]:#x})")
src = open(store_cpp).read()
need = int(re.search(r"SLOTS\s*=\s*(\d+)", src).group(1)) * int(re.search(r"SLOT_SIZE\s*=\s*(\w+)", src).group(1), 0)
if need > byname["prayer"][4]:
    errors.append(f"prayer slots need {need:#x}, partition is {byname['prayer'][4]:#x}")
if errors:
    sys.exit("partitions.csv:\n  " + "\n  ".join(errors))
print(f"partition table: {len(rows)} partitions, ends at {end:#x} of {FLASH:#x}, component regions fit")
EOF
# ESP-IDF's own checker too, when an ESPHome build has downloaded it (build_firmware.sh does).
GEN=$(ls -d "$HOME"/Library/Caches/esphome/idf/frameworks/*/components/partition_table/gen_esp32part.py \
             "$HOME"/.cache/esphome/idf/frameworks/*/components/partition_table/gen_esp32part.py 2>/dev/null | tail -1 || true)
if [ -n "$GEN" ]; then
  python3 "$GEN" -q --offset 0x8000 --primary-bootloader-offset 0x0 --flash-size 16MB ../partitions.csv "$OUT/pt.bin"
  echo "partition table: accepted by ESP-IDF's gen_esp32part.py"
fi

# Time zone vectors from Python's zoneinfo: every 30 minutes over 2025-2028 for the zones the data uses.
python3 - "$OUT/tz_vectors.txt" <<'EOF'
import sys, datetime as dt
from zoneinfo import ZoneInfo
cases = [("PST8PDT,M3.2.0,M11.1.0", "America/Los_Angeles"),
         ("EET-2EEST,M4.5.5/0,M10.5.4/24", "Africa/Cairo"),
         ("CET-1CEST,M3.5.0,M10.5.0/3", "Europe/Berlin"),
         ("<+03>-3", "Asia/Riyadh")]
with open(sys.argv[1], "w") as f:
    for tzs, iana in cases:
        z = ZoneInfo(iana)
        t = int(dt.datetime(2025, 1, 1, tzinfo=dt.timezone.utc).timestamp())
        end = int(dt.datetime(2029, 1, 1, tzinfo=dt.timezone.utc).timestamp())
        while t < end:
            off = -int(dt.datetime.fromtimestamp(t, z).utcoffset().total_seconds())
            f.write(f"{tzs}|{t}|{off}\n")
            t += 1800
EOF

CXX="${CXX:-c++}"
"$CXX" -std=c++17 -O1 -Wall -Wextra -o "$OUT/test_helpers" \
  test_helpers.cpp ../components/athan/tz_posix.cpp ../components/athan/mp3_check.cpp

ARGS=("$OUT/tz_vectors.txt")
for f in "$@"; do
  if command -v ffprobe >/dev/null; then
    ms=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$f" | awk '{printf "%d", $1*1000}')
    ARGS+=("$f" "$ms")
  fi
done
"$OUT/test_helpers" "${ARGS[@]}"
