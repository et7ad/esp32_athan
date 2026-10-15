#!/bin/bash
# Host unit tests (no ESP32 needed): firmware/partitions.csv and the component's flash regions, the TFT's icon
# glyphs, components/athan/ tz_posix.cpp, mp3_check.cpp, day_window.h, menu.cpp and tft_draw.cpp, the data in docs/
# (check_docs.py), and, when ArduinoJson is on this computer, the clock's own yearly-file checks (prayer_store.cpp).
# Needs: python3 (standard library), a C++17 compiler (c++/clang++/g++). Optional: ffprobe + some MP3 files.
#
#   firmware/tests/run_tests.sh                 # everything above
#   firmware/tests/run_tests.sh a.mp3 b.mp3     # also check MP3 durations against ffprobe (paths from where you are)
#   ARDUINOJSON=/path/to/ArduinoJson.h firmware/tests/run_tests.sh   # where to find ArduinoJson (or its src/ folder)
set -euo pipefail
# MP3 paths are relative to where the script was started: make them absolute before the cd below.
MP3S=()
for f in "$@"; do
  case "$f" in /*) ;; *) f="$PWD/$f" ;; esac
  [ -f "$f" ] || { echo "no such MP3 file: $f" >&2; exit 1; }
  MP3S+=("$f")
done
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}/athan_tests"
mkdir -p "$OUT"

# Partition table: what ESP-IDF's gen_esp32part.py checks at build time (overlaps, alignment, flash size), plus what
# the athan component hard-codes: the four sound regions (audio_slots.cpp: ascending, disjoint, 4 KB aligned, each
# holding its header sector and the largest file audio_slots.h allows, inside the partition), the prayer slots
# (prayer_store.cpp), and the partitions it looks up by type, subtype and name.
python3 - ../partitions.csv ../components/athan/audio_slots.h ../components/athan/audio_slots.cpp \
  ../components/athan/prayer_store.h ../components/athan/prayer_store.cpp <<'EOF'
import re, sys
csv, slots_h, slots_cpp, store_h, store_cpp = sys.argv[1:6]
FLASH = 16 * 1024 * 1024
rows = []
for n, line in enumerate(open(csv), 1):
    line = line.split("#", 1)[0].strip()
    if not line:
        continue
    f = [x.strip() for x in line.split(",")]
    rows.append((n, f[0], f[1], f[2], int(f[3], 0), int(f[4], 0)))
errors = []
end = 0x9000  # partition table at 0x8000-0x9000
for n, name, typ, sub, off, size in rows:
    align = 0x10000 if typ == "app" else 0x1000
    if off < end:
        errors.append(f"line {n} {name}: starts at {off:#x}, overlaps the previous partition (ends {end:#x})")
    if off % align or size % 0x1000:
        errors.append(f"line {n} {name}: offset {off:#x} / size {size:#x} not aligned to {align:#x}")
    end = off + size
if end > FLASH:
    errors.append(f"table ends at {end:#x}, beyond 16 MB")
byname = {r[1]: r for r in rows}
for need in ("audio", "prayer"):
    if need not in byname:
        sys.exit(f"partitions.csv: no {need!r} partition (the athan component needs it)")

def grab(path, pattern, what):
    m = re.search(pattern, open(path).read())
    if not m:
        sys.exit(f"partitions.csv check: cannot read {what} from {path}")
    return m.group(1)

def numbers(text):
    return [int(x.strip().rstrip("uUlL"), 0) for x in text.split(",") if x.strip()]

offs = numbers(grab(slots_cpp, r"REGION_OFFSET\[[^]]*\]\s*=\s*\{([^}]*)\}", "REGION_OFFSET"))
sizes = numbers(grab(slots_cpp, r"REGION_SIZE\[[^]]*\]\s*=\s*\{([^}]*)\}", "REGION_SIZE"))
header = int(grab(slots_cpp, r"\bHEADER_SECTOR\s*=\s*(\w+)", "HEADER_SECTOR"), 0)
limits = numbers(grab(slots_h, r"SLOT_MAX_BYTES\[[^]]*\]\s*=\s*\{([^}]*)\}", "SLOT_MAX_BYTES"))
names = [x.strip().strip('"') for x in grab(slots_h, r"SLOT_NAMES\[[^]]*\]\s*=\s*\{([^}]*)\}", "SLOT_NAMES").split(",")]
if not len(offs) == len(sizes) == len(limits) == len(names):
    errors.append(f"audio_slots: {len(offs)} offsets, {len(sizes)} sizes, {len(limits)} limits, {len(names)} names")
for i, (off, size, limit, name) in enumerate(zip(offs, sizes, limits, names)):
    if off % 0x1000 or size % 0x1000:
        errors.append(f"audio region {name}: offset {off:#x} / size {size:#x} not 4 KB aligned (flash erase sectors)")
    if size < header + limit:
        errors.append(f"audio region {name}: {size:#x} bytes cannot hold the {header}-byte header and a "
                      f"{limit}-byte file (AudioSlots::write() erases header + file with no region check)")
    if i + 1 < len(offs) and off + size > offs[i + 1]:
        errors.append(f"audio region {name} ends at {off + size:#x}, past the start of {names[i + 1]} ({offs[i + 1]:#x})")
if offs and offs[-1] + sizes[-1] > byname["audio"][5]:
    errors.append(f"audio regions end at {offs[-1] + sizes[-1]:#x}, beyond the audio partition ({byname['audio'][5]:#x})")
slots = int(grab(store_cpp, r"\bSLOTS\s*=\s*(\d+)", "SLOTS"))
slot_size = int(grab(store_cpp, r"\bSLOT_SIZE\s*=\s*(\w+)", "SLOT_SIZE"), 0)
table_offset = int(grab(store_cpp, r"\bTABLE_OFFSET\s*=\s*(\w+)", "TABLE_OFFSET"), 0)
fields = int(grab(store_h, r"\bNUM_FIELDS\s*=\s*(\d+)", "NUM_FIELDS"))
if slot_size % 0x1000 or table_offset % 0x1000:
    errors.append(f"prayer slots: size {slot_size:#x} / table offset {table_offset:#x} not 4 KB aligned")
if table_offset + 366 * fields * 2 > slot_size:
    errors.append(f"prayer slot {slot_size:#x} cannot hold a leap year's table ({366 * fields * 2} bytes at {table_offset:#x})")
if slots * slot_size > byname["prayer"][5]:
    errors.append(f"prayer slots need {slots * slot_size:#x}, partition is {byname['prayer'][5]:#x}")
# The partitions the component looks up: esp_partition_find_first(ESP_PARTITION_TYPE_DATA, <SUBTYPE>, "<name>")
looked_up = 0
for path in (slots_cpp, store_cpp):
    src = open(path).read()
    subtypes = dict(re.findall(r"(\w+)\s*=\s*static_cast<esp_partition_subtype_t>\((0x[0-9A-Fa-f]+)\)", src))
    for var, label in re.findall(r"esp_partition_find_first\(ESP_PARTITION_TYPE_DATA,\s*(\w+),\s*\"([^\"]+)\"\)", src):
        looked_up += 1
        row = byname.get(label)
        if var not in subtypes:
            errors.append(f"{path}: cannot read the subtype {var} it looks up {label!r} with")
        elif row is None or row[2] != "data" or int(row[3], 0) != int(subtypes[var], 0):
            errors.append(f"{path} looks up a data partition {label!r} with subtype {subtypes[var]}; partitions.csv has "
                          + (f"{row[2]}, {row[3]}" if row else "none"))
if looked_up != 2:
    errors.append(f"expected the audio and prayer partition lookups in the component, found {looked_up}")
if errors:
    sys.exit("partitions.csv:\n  " + "\n  ".join(errors))
print(f"partition table: {len(rows)} partitions, ends at {end:#x} of {FLASH:#x}; {len(offs)} sound regions and "
      f"{slots} prayer slots fit, partitions looked up by subtype and name match")
EOF
# ESP-IDF's own checker too, when an ESPHome build has downloaded it (build_firmware.sh does).
GEN=$(ls -d "$HOME"/Library/Caches/esphome/idf/frameworks/*/components/partition_table/gen_esp32part.py \
             "$HOME"/.cache/esphome/idf/frameworks/*/components/partition_table/gen_esp32part.py 2>/dev/null | tail -1 || true)
if [ -n "$GEN" ]; then
  python3 "$GEN" -q --offset 0x8000 --primary-bootloader-offset 0x0 --flash-size 16MB ../partitions.csv "$OUT/pt.bin"
  echo "partition table: accepted by ESP-IDF's gen_esp32part.py"
fi

# The TFT's icons: every Material Symbols code point tft_theme.h names must be a glyph of an icon font in
# displays/tft.yaml (a missing glyph draws nothing on the device, without an error).
python3 - ../components/athan/tft_theme.h ../displays/tft.yaml <<'EOF'
import re, sys
theme, yaml = open(sys.argv[1]).read(), open(sys.argv[2]).read()
used = {m.group(2).lower(): m.group(1) for m in re.finditer(r'constexpr const char \*(\w+) = "\\u([0-9a-fA-F]{4})"', theme)}
listed = {c.lower() for c in re.findall(r'"\\u([0-9a-fA-F]{4})"', yaml)}
missing = sorted(f"{name} (\\u{cp})" for cp, name in used.items() if cp not in listed)
if not used or missing:
    sys.exit("tft icons missing from displays/tft.yaml's icon fonts: " + (", ".join(missing) or "none found in tft_theme.h"))
print(f"tft icons: all {len(used)} code points of tft_theme.h are in displays/tft.yaml")
EOF

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
if [ ${#MP3S[@]} -gt 0 ] && ! command -v ffprobe >/dev/null; then
  echo "mp3: ffprobe not found, the ${#MP3S[@]} MP3 file(s) are not checked (macOS: brew install ffmpeg)"
fi
for f in ${MP3S[@]+"${MP3S[@]}"}; do
  if command -v ffprobe >/dev/null; then
    ms=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$f" | awk '{printf "%d", $1*1000}')
    ARGS+=("$f" "$ms")
  fi
done
"$OUT/test_helpers" "${ARGS[@]}"

# Device menu navigation (components/athan/menu.cpp): rows, ends, wrapping, Select per row kind, lists that change.
"$CXX" -std=c++17 -O1 -Wall -Wextra -o "$OUT/test_menu" test_menu.cpp ../components/athan/menu.cpp
"$OUT/test_menu"

# The TFT's ring drawing (components/athan/tft_draw.cpp, against a host stand-in for ESPHome's display.h): the fast
# arc() and ring() draw the same pixels as a plain scan with the angle of every pixel.
"$CXX" -std=c++17 -O1 -Wall -Wextra -I host_display -o "$OUT/test_tft_draw" test_tft_draw.cpp \
  ../components/athan/tft_draw.cpp
"$OUT/test_tft_draw"

# The clock's own yearly-file checks (prayer_store.cpp parse_year_file, tz_posix.cpp parse_posix_tz) on the computer,
# with stand-ins for ESPHome and ESP-IDF headers (host_prayer/). Needs ArduinoJson, found without the network: in
# ARDUINOJSON, in a build_firmware.sh build, or where syntax_check.sh downloaded it. Skipped otherwise.
AJ=""
for c in ${ARDUINOJSON:+"$ARDUINOJSON"} \
         "$HOME"/.cache/esp32_athan/data/build/*/managed_components/bblanchon__arduinojson/src \
         "$HOME"/.cache/esp32_athan/data/.espressif/*/bblanchon__arduinojson*/src \
         "${TMPDIR:-/tmp}/athan_syntax_check/stubs"; do
  [ -f "$c" ] && c="$(dirname "$c")"
  if [ -f "$c/ArduinoJson.h" ]; then AJ="$c"; break; fi
done
HARNESS=()
if [ -n "$AJ" ]; then
  "$CXX" -std=c++17 -O1 -Wall -Wextra -I host_prayer -I "$AJ" -o "$OUT/test_year_files" test_year_files.cpp \
    ../components/athan/prayer_store.cpp ../components/athan/tz_posix.cpp
  HARNESS=(--harness "$OUT/test_year_files")
else
  echo "yearly files: the clock's own parse_year_file() not run (no ArduinoJson.h here; set ARDUINOJSON=...)"
fi

# The data every clock downloads (docs/): lists, stations, mosque keys everywhere they are listed, yearly files.
python3 check_docs.py ${HARNESS[@]+"${HARNESS[@]}"}
