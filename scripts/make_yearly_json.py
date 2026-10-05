#!/usr/bin/env python3
"""Convert the V2 daily prayer files into version 3 yearly files (one file per mosque per year).

The V2 repository (et7ad/esp_athan) keeps one JSON per day, docs/athantimes/<key>/<year>/<DDD>.json, because
deployed V2 devices read them there. This script reads those days and writes ONE positional file per year into
this repository: docs/athantimes/<key>/<year>.json (format: prayertimes_specs.md).

Use it for years that only exist in the V2 repository. New timetables go straight to a yearly file with
source_to_yearly.py, parse_Islamicfinder/parse_islamicfinder_yearly.py or santaclara_prayer_times_parser.py.

Usage (from the root of this repository, V2 repository checked out next to it as ../esp_athan):
    python3 scripts/make_yearly_json.py                         # every mosque and year found
    python3 scripts/make_yearly_json.py --key davis --year 2027
    python3 scripts/make_yearly_json.py --v2-root /path/to/esp_athan --dry-run

The daily files carry afternoon times in 12-hour form; they are converted with the same rules V2 firmware used
(athan_yearly.PM_BELOW). Exit code 1 if any year failed (nothing is written for a failed year).
"""

from __future__ import annotations

import argparse
import calendar
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from athan_yearly import (DataError, OUT_ROOT, REPO, day_from_times, mosque, write_year)  # noqa: E402


def read_daily_year(year_dir: pathlib.Path, key: str, year: int, warnings: list) -> list:
    ndays = 366 if calendar.isleap(year) else 365
    rows = []
    for n in range(1, ndays + 1):
        p = year_dir / f"{n:03d}.json"
        if not p.exists():
            raise DataError(f"{p}: missing")
        rows.append(day_from_times(json.loads(p.read_text()), f"{key}/{year}/{n:03d}", warnings,
                                   ambiguous_12h=True))
    return rows


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--v2-root", type=pathlib.Path, default=REPO.parent / "esp_athan",
                    help="checkout of the V2 repository (default: ../esp_athan)")
    ap.add_argument("--out-root", type=pathlib.Path, default=OUT_ROOT)
    ap.add_argument("--key", action="append", help="only this mosque key (repeatable)")
    ap.add_argument("--year", action="append", type=int, help="only this year (repeatable)")
    ap.add_argument("--max-drift", type=int, default=3, help="minutes; year-to-year adhan check (default 3)")
    ap.add_argument("--dry-run", action="store_true", help="check only, write nothing")
    a = ap.parse_args()

    daily_root = a.v2_root / "docs" / "athantimes"
    if not daily_root.is_dir():
        print(f"V2 daily data not found at {daily_root} (use --v2-root)", file=sys.stderr)
        return 1

    failed = 0
    for key_dir in sorted(p for p in daily_root.iterdir() if p.is_dir()):
        key = key_dir.name
        if a.key and key not in a.key:
            continue
        years = sorted(int(p.name) for p in key_dir.iterdir() if p.is_dir() and p.name.isdigit())
        for year in years:
            if a.year and year not in a.year:
                continue
            warnings: list = []
            try:
                tz_text = mosque(key)["tz"]
                rows = read_daily_year(key_dir / str(year), key, year, warnings)
                prev_rows = None
                if (year - 1) in years:  # compare against the V2 data of the year before when it exists
                    prev_rows = read_daily_year(key_dir / str(year - 1), key, year - 1, [])
                write_year(key, year, rows, tz_text=tz_text, warnings=warnings, out_root=a.out_root,
                           max_drift=a.max_drift, dry_run=a.dry_run, prev_rows=prev_rows)
            except (DataError, KeyError, ValueError) as e:
                print(f"✗ {key}/{year}: {e}")
                failed += 1
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
