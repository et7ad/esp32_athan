#!/bin/bash
# Host unit tests for firmware/components/athan/tz_posix.cpp and mp3_check.cpp (pure C++, no ESP32 needed).
# Needs: python3 (standard library), a C++17 compiler (c++/clang++/g++). Optional: ffprobe + some MP3 files.
#
#   firmware/tests/run_tests.sh                 # calendar, parser and time zone checks
#   firmware/tests/run_tests.sh a.mp3 b.mp3     # also check MP3 durations against ffprobe
set -euo pipefail
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}/athan_tests"
mkdir -p "$OUT"

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
