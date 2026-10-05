#!/usr/bin/env python3
"""Turn a mosque's yearly timetable table into ONE version 3 yearly file.

Replaces the V2 break_json.py, which wrote 365 daily files. Output: docs/athantimes/<key>/<year>.json
(format: prayertimes_specs.md). One run = one file.

Input: a JSON array whose first element is the header row and the rest are days, e.g. exported from a mosque's
spreadsheet:

    [["Year","Month","#","Day","Fajr","Iqa.","Sunrise","Dhuhar","Iqa.","Asr","Iqa.","Maghrib","Iqa.","Isha","Iqa."],
     ["2027","1","Friday","1","6:06","6:31","7:24","12:14","12:24","2:39","2:49","4:57","5:12","6:16","6:31"],
     ...]

Header cells are matched case-insensitively; "Iqa." means the iqama of the prayer column before it. Doha may be
a column; when it is missing it becomes sunrise + doha_after_sunrise from docs/athantimes/mosques.json. Missing
iqama columns become adhan + iqama_after_default (or the mosque's own "iqama_after"). Times may be written
without AM/PM (afternoon hours below 12, as most printed timetables do); add --24h if the source is already
24-hour.

Usage:
    python3 scripts/source_to_yearly.py --key davis path/to/davis_2027.json
    python3 scripts/source_to_yearly.py --key davis path/to/davis_2027.json --dry-run
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from athan_yearly import DataError, FIELDS, MOSQUES_FILE, day_from_times, mosque, write_year  # noqa: E402

HEADER_ALIASES = {"dhuhr": "dhuhar", "zuhr": "dhuhar", "duha": "doha", "shrouk": "sunrise", "shuruq": "sunrise"}


def clean_headers(headers: list) -> list:
    out, last = [], None
    for h in headers:
        name = str(h).strip().lower()
        name = HEADER_ALIASES.get(name, name)
        if name in ("iqa.", "iqa", "iqama", "iqamah"):
            out.append(f"{last}_iqa" if last else "iqa")
        else:
            out.append(name)
            if name in FIELDS:
                last = name
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", type=pathlib.Path)
    ap.add_argument("--key", required=True, help=f"mosque key, must exist in {MOSQUES_FILE}")
    ap.add_argument("--24h", dest="is_24h", action="store_true", help="source times are already 24-hour")
    ap.add_argument("--dry-run", action="store_true", help="check only, write nothing")
    ap.add_argument("--max-drift", type=int, default=3, help="minutes; year-to-year adhan check (default 3)")
    a = ap.parse_args()

    try:
        info = mosque(a.key)
        registry = json.loads(MOSQUES_FILE.read_text())
        iqama_after = info.get("iqama_after", registry["iqama_after_default"])
        data = json.loads(a.input.read_text(encoding="utf-8"))
        headers, raw_rows = clean_headers(data[0]), data[1:]
        for need in ("year", "month", "day"):
            if need not in headers:
                raise DataError(f"header has no {need!r} column: {data[0]}")
        warnings: list = []
        days = []
        for i, raw in enumerate(raw_rows, start=1):
            rec = {k: (v.strip() if isinstance(v, str) else v) for k, v in zip(headers, raw)}
            where = f"{a.input.name} row {i} ({rec.get('year')}-{rec.get('month')}-{rec.get('day')})"
            date = (int(rec["year"]), int(rec["month"]), int(rec["day"]))
            days.append((date, day_from_times(rec, where, warnings, ambiguous_12h=not a.is_24h,
                                              doha_after_sunrise=info.get("doha_after_sunrise"),
                                              iqama_after=iqama_after)))
        days.sort()
        years = {d[0][0] for d in days}
        if len(years) != 1:
            raise DataError(f"the table mixes years {sorted(years)}; one table must be one calendar year")
        year = years.pop()
        if len({d[0] for d in days}) != len(days):
            raise DataError("the table lists a date twice")
        write_year(a.key, year, [r for _, r in days], warnings=warnings, max_drift=a.max_drift, dry_run=a.dry_run)
    except (DataError, KeyError, ValueError, IndexError) as e:
        print(f"✗ {e}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
