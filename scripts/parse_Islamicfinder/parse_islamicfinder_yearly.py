#!/usr/bin/env python3
"""Parse an IslamicFinder *yearly* HTML export into ONE version 3 yearly prayer file.

IslamicFinder's yearly page renders one ``.month-block`` per Gregorian month. Each block holds a
``.prayer-table`` whose header row is::

    Day | Fajr | Sunrise | Dhuhr | Asr | Maghrib | Isha

and one ``.prayer-row`` per day, with times in 12-hour ``HH:MM AM/PM`` form (the AM/PM marker makes them
unambiguous). The source carries only adhan times, so the rest is derived from docs/athantimes/mosques.json:

    * doha  = sunrise + the mosque's doha_after_sunrise
    * *_iqa = adhan + iqama_after_default (or the mosque's own "iqama_after")

Output: docs/athantimes/<key>/<year>.json (format: prayertimes_specs.md, one positional list per day).

Usage
-----
    # one file, explicit key
    python3 scripts/parse_Islamicfinder/parse_islamicfinder_yearly.py --key masjid15 path/to/Visalia2026.html

    # every *.html under ./preparing/ (all for the same key)
    python3 scripts/parse_Islamicfinder/parse_islamicfinder_yearly.py --key masjid15

How to save the HTML from the browser: see parse_islamicfinder_yearly_get_html.jpg next to this script.
"""

import argparse
import calendar
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from athan_yearly import DataError, MOSQUES_FILE, day_from_times, mosque, write_year  # noqa: E402

try:
    from bs4 import BeautifulSoup
except ImportError:
    sys.exit(
        "This script needs BeautifulSoup. Install it with:\n"
        "    pip install beautifulsoup4"
    )

# Directory scanned for *.html when no path is given on the command line.
DEFAULT_INPUT_DIR = pathlib.Path(__file__).resolve().parent / "preparing"

# Header cells the source is expected to expose, in order.
EXPECTED_HEADER = ["Day", "Fajr", "Sunrise", "Dhuhr", "Asr", "Maghrib", "Isha"]

MONTH_NAME_TO_NUMBER = {name: i for i, name in enumerate(calendar.month_name) if name}


def big_error(msg):
    """Print a loud banner and stop."""
    bar = "!" * 80
    print("\n" + bar)
    print("!!!!!  FATAL ERROR — STOPPING  !!!!!".center(80))
    print(bar)
    print(msg)
    print(bar + "\n")
    sys.exit(1)


def parse_month_block(block, html_path):
    """Return (year, month_number, [row_dicts]) for one .month-block.

    Each row_dict has raw source fields (day + 6 adhan times, still 12-hour).
    """
    name_el = block.select_one(".monthNameeng")
    if name_el is None:
        big_error(f"A month block in {html_path} has no .monthNameeng header.")
    header_text = name_el.get_text(strip=True)  # e.g. "January 2026"

    parts = header_text.split()
    if len(parts) != 2 or parts[0] not in MONTH_NAME_TO_NUMBER or not parts[1].isdigit():
        big_error(f"Unexpected month header {header_text!r} in {html_path}.")
    month_number = MONTH_NAME_TO_NUMBER[parts[0]]
    year = int(parts[1])

    rows = block.select(".prayer-row")
    if not rows:
        big_error(f"Month block {header_text!r} in {html_path} has no rows.")

    # First .prayer-row is the header (class 'prayer-row prayer-header').
    header_cells = [d.get_text(strip=True) for d in rows[0].find_all("div")]
    if header_cells != EXPECTED_HEADER:
        big_error(
            f"Header mismatch in {header_text!r} ({html_path}).\n"
            f"  expected: {EXPECTED_HEADER}\n"
            f"  found:    {header_cells}"
        )

    out_rows = []
    for row in rows[1:]:
        cells = [d.get_text(strip=True) for d in row.find_all("div")]
        if len(cells) != len(EXPECTED_HEADER):
            big_error(
                f"{header_text} in {html_path}: expected "
                f"{len(EXPECTED_HEADER)} cells, got {len(cells)}: {cells}"
            )
        out_rows.append({
            "day": cells[0],
            "fajr": cells[1],
            "sunrise": cells[2],
            "dhuhar": cells[3],
            "asr": cells[4],
            "maghrib": cells[5],
            "isha": cells[6],
        })

    return year, month_number, out_rows


def process_html(html_path, key, *, max_drift=3, dry_run=False):
    """Parse one IslamicFinder yearly HTML file and write one yearly file."""
    print(f"Processing {html_path}")
    soup = BeautifulSoup(html_path.read_text(encoding="utf-8"), "html.parser")

    blocks = soup.select(".month-block")
    if len(blocks) != 12:
        big_error(f"Expected 12 month blocks in {html_path}, found {len(blocks)}.")

    info = mosque(key)
    registry = json.loads(MOSQUES_FILE.read_text())
    iqama_after = info.get("iqama_after", registry["iqama_after_default"])

    file_year = None
    rows, warnings = [], []
    for index, block in enumerate(blocks, start=1):
        year, month_number, raw_rows = parse_month_block(block, html_path)
        if month_number != index:
            big_error(f"Month order mismatch in {html_path}: block #{index} is month {month_number}.")
        if file_year is None:
            file_year = year
        elif year != file_year:
            big_error(f"Mixed years in {html_path}: saw {file_year} and {year}. One calendar year per file.")
        expected_days = calendar.monthrange(year, month_number)[1]
        if len(raw_rows) != expected_days:
            big_error(f"{calendar.month_name[month_number]} {year} in {html_path}: "
                      f"expected {expected_days} days, found {len(raw_rows)}.")
        for raw in raw_rows:
            where = f"{year}-{month_number:02d}-{int(raw['day']):02d}"
            rows.append(day_from_times(raw, where, warnings, ambiguous_12h=False,
                                       doha_after_sunrise=info.get("doha_after_sunrise"),
                                       iqama_after=iqama_after))
    write_year(key, file_year, rows, warnings=warnings, max_drift=max_drift, dry_run=dry_run)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("html", nargs="*", type=pathlib.Path, help="IslamicFinder yearly HTML file(s)")
    ap.add_argument("--key", required=True, help=f"mosque key, must exist in {MOSQUES_FILE}")
    ap.add_argument("--dry-run", action="store_true", help="check only, write nothing")
    ap.add_argument("--max-drift", type=int, default=3, help="minutes; year-to-year adhan check (default 3)")
    a = ap.parse_args(argv[1:])
    inputs = a.html or sorted(DEFAULT_INPUT_DIR.glob("*.html"))
    if not inputs:
        big_error(f"No *.html files found in {DEFAULT_INPUT_DIR}. Pass an HTML path explicitly.")
    for html_path in inputs:
        if not html_path.is_file():
            big_error(f"Input file not found: {html_path}")
        try:
            process_html(html_path.resolve(), a.key, max_drift=a.max_drift, dry_run=a.dry_run)
        except DataError as e:
            big_error(str(e))


if __name__ == "__main__":
    main(sys.argv)
