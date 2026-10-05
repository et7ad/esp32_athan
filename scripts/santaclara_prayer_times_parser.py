#!/usr/bin/env python3
"""Parse an MCA (mcabayarea.org) yearly prayer-time PDF into ONE version 3 yearly file.

The PDF holds 12 monthly tables (columns: day, fajr, fajr iqama, sunrise, dhuhr, dhuhr iqama, asr, asr iqama,
maghrib, isha, isha iqama). Afternoon times are printed without AM/PM; they are converted with the same rules as
every other source (athan_yearly.PM_BELOW). Doha = sunrise + the mosque's doha_after_sunrise and Maghrib iqama =
Maghrib + 5 min (neither is in the PDF).

Output: docs/athantimes/<key>/<year>.json (prayertimes_specs.md). The year is the first 4 characters of the PDF
file name, e.g. 2027_MCA_Prayer_Time.pdf.

Usage:
    python3 scripts/santaclara_prayer_times_parser.py --key sclaramca path/to/2027_MCA_Prayer_Time.pdf
    python3 scripts/santaclara_prayer_times_parser.py --key sclaraalnoor path/to/2027_AlNoor_Prayer_Time.pdf
"""
import argparse
import calendar
import json
import pathlib
import sys
from datetime import datetime, timedelta

import pdfplumber

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from athan_yearly import DataError, day_from_times, mosque, write_year  # noqa: E402

MAGHRIB_IQAMA_AFTER = 5


MONTH_NAMES = [
    "January",
    "February",
    "March",
    "April",
    "May",
    "June",
    "July",
    "August",
    "September",
    "October",
    "November",
    "December",
]



def extract_tables_from_pdf(pdf_path):
    """Extract 12 monthly tables from a PDF and convert to validated dicts.

    - Each table's first row contains a header whose first cell is the month name.
    - Output objects contain:
        {"month": <month_name>, "rows": <data_rows>}
    - The first two data rows (after the header row) are discarded, as they
      correspond to header information that will be defined manually later.
    - Asserts:
        * Exactly 12 tables are found in the file.
        * The i-th table corresponds to the i-th calendar month.
    """

    monthly_tables = []

    with pdfplumber.open(pdf_path) as pdf:
        month_index = 0

        for page in pdf.pages:
            tables = page.extract_tables() or []
            for table in tables:
                # Skip completely empty tables, if any
                if not table or not table[0]:
                    continue

                month_index += 1

                if month_index > 12:
                    raise AssertionError(
                        f"Expected exactly 12 tables, found more than 12 in {pdf_path}"
                    )

                header_row = table[0]
                month_cell = header_row[0]

                if month_cell is None or str(month_cell).strip() == "":
                    raise AssertionError(
                        f"Missing month name in table {month_index} of {pdf_path}"
                    )

                month_name = str(month_cell).strip()
                expected_month = MONTH_NAMES[month_index - 1]

                if month_name.lower() != expected_month.lower():
                    raise AssertionError(
                        "Month/table order mismatch: "
                        f"table {month_index} has '{month_name}', "
                        f"expected '{expected_month}' in {pdf_path}"
                    )

                # Data rows are everything after the first header row. The
                # first two of those rows are themselves header rows that
                # will be defined manually, so skip them.
                data_rows = table[1:] if len(table) > 1 else []
                if len(data_rows) > 2:
                    data_rows = data_rows[2:]
                else:
                    data_rows = []

                table_data = {
                    "month": month_name,
                    "month_number": month_index,
                    "rows": data_rows,
                }
                monthly_tables.append(table_data)

    if month_index != 12:
        raise AssertionError(
            f"Expected exactly 12 tables in {pdf_path}, found {month_index}"
        )

    return monthly_tables

# the source data:
# every row has following format   
# "1", day of month
# "6:05", fajr athan
# "6:30", fajr iqama
# "7:22", sunrise (doha doesnt exist make it 15 mins after sunrise)
# "12:15", dhuhr athan
# "12:35", dhuhr iqama
# "2:44", asr athan
# "3:15", asr iqama
# "5:04", maghrib athan
# "6:19", isha athan
# "8:00", isha iqama

# target format: the field "#" is not needed because doesnt exist in the source.
# {
#   "year": "2025", # year defined in this file at beginning
#   "month": "1", # month number is defined in the dictionary when the function extract_tables_from_pdf is used
#   "#": "Wednesday", # doesnt exist in source, compute it based on the calender imported and the date
#   "day": "1", # first entry in the row
#   "fajr": "06:06", # fajr athan
#   "fajr_iqa": "06:36", # fajr iqama
#   "sunrise": "07:24", # sunrise
#   "doha": "07:39", # doha time (not in source, can be calculated as 15 mins after sunrise)
#   "dhuhar": "12:14", # dhuhr athan
#   "dhuhar_iqa": "12:34", # dhuhr iqama
#   "asr": "02:39", # asr athan
#   "asr_iqa": "02:59", # asr iqama
#   "maghrib": "04:57", # maghrib athan
#   "maghrib_iqa": "05:09", # doesnt exist in source and not needed, so calculate it 5 minutes after maghrib athan
#   "isha": "06:16", # isha athan
#   "isha_iqa": "07:10" # isha iqama
# }


def _normalize_time(time_str):
    """Normalize a time string to HH:MM (24-hour) if possible.

    Returns the original string if it can't be safely parsed as H:MM/HH:MM.
    """

    if not time_str:
        return time_str

    s = str(time_str).strip()
    parts = s.split(":")
    if len(parts) != 2:
        return s

    h, m = parts[0].strip(), parts[1].strip()
    if not (h.isdigit() and m.isdigit()):
        return s

    hours = int(h)
    minutes = int(m)
    if not (0 <= hours < 24 and 0 <= minutes < 60):
        return s

    return f"{hours:02d}:{minutes:02d}"


def _add_minutes_to_time(time_str, minutes):
    """Return time_str offset by given minutes, formatted as HH:MM.

    If parsing fails or time_str is empty, returns the original time_str.
    """

    if not time_str:
        return time_str

    try:
        base_str = _normalize_time(time_str)
        base = datetime.strptime(base_str, "%H:%M")
        new_time = base + timedelta(minutes=minutes)
        return new_time.strftime("%H:%M")
    except Exception:
        return time_str


def process_pdf(pdf_file: pathlib.Path, key: str, *, max_drift: int = 3, dry_run: bool = False) -> None:
    """Extract the 12 monthly tables of one PDF and write one yearly file."""
    year_str = pdf_file.stem[:4]
    if not year_str.isdigit():
        raise DataError(f"{pdf_file}: cannot read the year from the file name (expected e.g. 2027_MCA_...)")
    year = int(year_str)
    info = mosque(key)
    print(f"Processing {pdf_file} -> {key}/{year}")

    days = {}
    warnings = []
    for table in extract_tables_from_pdf(pdf_file):
        month_number = table.get("month_number")
        if not month_number:
            raise DataError(f"{pdf_file}: a table has no month number")
        for row in table.get("rows", []):
            if not (row and len(row) >= 11 and row[0] is not None and str(row[0]).strip().isdigit()):
                continue
            day_num = int(str(row[0]).strip())
            date = datetime(year, month_number, day_num)
            where = f"{pdf_file.name} {date.date().isoformat()}"
            times = {
                "fajr": _normalize_time(row[1]),
                "fajr_iqa": _normalize_time(row[2]),
                "sunrise": _normalize_time(row[3]),
                "dhuhar": _normalize_time(row[4]),
                "dhuhar_iqa": _normalize_time(row[5]),
                "asr": _normalize_time(row[6]),
                "asr_iqa": _normalize_time(row[7]),
                "maghrib": _normalize_time(row[8]),
                "maghrib_iqa": _add_minutes_to_time(row[8], MAGHRIB_IQAMA_AFTER),
                "isha": _normalize_time(row[9]),
                "isha_iqa": _normalize_time(row[10]) if len(row) > 10 else "",
            }
            days[date.timetuple().tm_yday] = day_from_times(
                times, where, warnings, ambiguous_12h=True, doha_after_sunrise=info.get("doha_after_sunrise"))

    ndays = 366 if calendar.isleap(year) else 365
    missing = [d for d in range(1, ndays + 1) if d not in days]
    if missing:
        raise DataError(f"{pdf_file}: {len(missing)} day(s) missing, first day-of-year {missing[0]}")
    write_year(key, year, [days[d] for d in range(1, ndays + 1)], warnings=warnings, max_drift=max_drift,
               dry_run=dry_run)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pdf", nargs="+", type=pathlib.Path)
    ap.add_argument("--key", required=True, help="mosque key in docs/athantimes/mosques.json")
    ap.add_argument("--dry-run", action="store_true", help="check only, write nothing")
    ap.add_argument("--max-drift", type=int, default=3, help="minutes; year-to-year adhan check (default 3)")
    a = ap.parse_args()
    failed = 0
    for pdf in a.pdf:
        try:
            process_pdf(pdf, a.key, max_drift=a.max_drift, dry_run=a.dry_run)
        except (DataError, AssertionError, ValueError) as e:
            print(f"✗ {e}")
            failed += 1
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
