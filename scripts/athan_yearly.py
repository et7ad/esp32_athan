"""Shared code for every script that produces prayer-time data in this repository.

Every producer writes exactly ONE file per mosque per year:

    docs/athantimes/<key>/<year>.json

in the positional format of prayertimes_specs.md: a small header plus a "days" list with one list of 12
"HH:MM" strings per day (index 0 = 1 January), in the order of FIELDS. No per-day files, no per-day keys.

Use from a script in this folder:

    from athan_yearly import day_from_times, write_year, mosque
    rows = [day_from_times({...}, where=..., warnings=w, doha_after_sunrise=..., ...) for each day]
    write_year(key, year, rows)              # validates, compares with the previous year, writes one file

Nothing here needs third-party packages.
"""

from __future__ import annotations

import calendar
import datetime as dt
import json
import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parent.parent
OUT_ROOT = REPO / "docs" / "athantimes"
MOSQUES_FILE = OUT_ROOT / "mosques.json"

# Column order of every day list. The firmware finds columns by this list (the file carries it as "fields").
FIELDS = ["fajr", "fajr_iqa", "sunrise", "doha", "dhuhar", "dhuhar_iqa",
          "asr", "asr_iqa", "maghrib", "maghrib_iqa", "isha", "isha_iqa"]
ADHAN_ORDER = ["fajr", "sunrise", "doha", "dhuhar", "asr", "maghrib", "isha"]
IQAMA_OF = {"fajr_iqa": "fajr", "dhuhar_iqa": "dhuhar", "asr_iqa": "asr",
            "maghrib_iqa": "maghrib", "isha_iqa": "isha"}
# Sources that print afternoon times without AM/PM (V2 daily files, the MCA PDFs, break_json-style tables):
# add 12 hours when the written hour is below this value. Fajr, sunrise and Doha are always morning.
PM_BELOW = {"dhuhar": 9, "dhuhar_iqa": 9, "asr": 12, "asr_iqa": 12,
            "maghrib": 12, "maghrib_iqa": 12, "isha": 12, "isha_iqa": 12}
DEFAULT_IQAMA_AFTER = {"fajr": 30, "dhuhar": 10, "asr": 10, "maghrib": 10, "isha": 20}
MAX_BYTES = 100_000  # the device refuses larger files

_TIME_RE = re.compile(r"^\s*(\d{1,2}):(\d{2})\s*(AM|PM|am|pm)?\s*$")
_STRICT_RE = re.compile(r"^([01][0-9]|2[0-3]):[0-5][0-9]$")


class DataError(ValueError):
    """A problem that stops a year from being written."""


# ------------------------------------------------------------------------------------------------------------
# Times
# ------------------------------------------------------------------------------------------------------------
def to_minutes(field: str, raw, where: str, warnings: list, ambiguous_12h: bool) -> int:
    """'HH:MM', 'H:MM' or 'HH:MM AM/PM' -> minutes after midnight.

    ambiguous_12h: the source prints afternoon times without AM/PM; PM_BELOW decides.
    """
    m = _TIME_RE.match(str(raw))
    if not m:
        raise DataError(f"{where}: {field} = {raw!r} is not a time")
    h, mi, ampm = int(m.group(1)), int(m.group(2)), m.group(3)
    if ampm:
        if not 1 <= h <= 12:
            raise DataError(f"{where}: {field} = {raw!r} is not a 12-hour time")
        h = (h % 12) + (12 if ampm.upper() == "PM" else 0)
    else:
        if not _STRICT_RE.match(str(raw).strip()):
            warnings.append(f"{where}: {field} = {raw!r} is not strict HH:MM (accepted)")
        if ambiguous_12h and field in PM_BELOW and h < PM_BELOW[field]:
            h += 12
    if not (0 <= h <= 23 and 0 <= mi <= 59):
        raise DataError(f"{where}: {field} = {raw!r} is out of range")
    return h * 60 + mi


def hhmm(minutes: int) -> str:
    return f"{minutes // 60:02d}:{minutes % 60:02d}"


def day_from_times(times: dict, where: str, warnings: list, *, ambiguous_12h: bool,
                   doha_after_sunrise: int | None = None, iqama_after: dict | None = None) -> list:
    """One day: a dict with any of FIELDS (strings) -> list of 12 minute values in FIELDS order.

    Missing doha = sunrise + doha_after_sunrise; missing iqama = adhan + iqama_after[prayer].
    Raises DataError when something required is missing or the adhan times are out of order.
    Appends a warning (and keeps the value) when an iqama is earlier than its adhan.
    """
    iqama_after = iqama_after or DEFAULT_IQAMA_AFTER
    mins = {}
    for f in FIELDS:
        v = times.get(f)
        if v is not None and str(v).strip() != "":
            mins[f] = to_minutes(f, v, where, warnings, ambiguous_12h)
    for f in ("fajr", "sunrise", "dhuhar", "asr", "maghrib", "isha"):
        if f not in mins:
            raise DataError(f"{where}: {f} is missing")
    if "doha" not in mins:
        if doha_after_sunrise is None:
            raise DataError(f"{where}: doha is missing and no doha_after_sunrise is set")
        mins["doha"] = mins["sunrise"] + doha_after_sunrise
    for iq, ad in IQAMA_OF.items():
        if iq not in mins:
            mins[iq] = mins[ad] + iqama_after[ad]
    for f, v in mins.items():
        if v >= 24 * 60:
            raise DataError(f"{where}: {f} runs past midnight ({hhmm(v % 1440)} next day)")
    adhan = [mins[f] for f in ADHAN_ORDER]
    if any(a >= b for a, b in zip(adhan, adhan[1:])):
        raise DataError(f"{where}: adhan times out of order: " +
                        ", ".join(f"{f} {hhmm(v)}" for f, v in zip(ADHAN_ORDER, adhan)))
    for iq, ad in IQAMA_OF.items():
        if mins[iq] < mins[ad]:
            warnings.append(f"{where}: {iq} {hhmm(mins[iq])} is before {ad} {hhmm(mins[ad])} (kept as published)")
    return [mins[f] for f in FIELDS]


# ------------------------------------------------------------------------------------------------------------
# Mosques registry: docs/athantimes/mosques.json  (time zone, name and data defaults per key)
# ------------------------------------------------------------------------------------------------------------
def mosques() -> dict:
    return json.loads(MOSQUES_FILE.read_text())["mosques"]


def mosque(key: str) -> dict:
    m = mosques()
    if key not in m:
        raise DataError(f"mosque key {key!r} is not in {MOSQUES_FILE} (add it there first)")
    return m[key]


# ------------------------------------------------------------------------------------------------------------
# POSIX TZ ("PST8PDT,M3.2.0,M11.1.0"), same rules as firmware/components/athan/tz_posix.cpp.
# Offsets are seconds WEST of UTC, as in POSIX. Used for the year-to-year check.
# ------------------------------------------------------------------------------------------------------------
class PosixTZ:
    def __init__(self, text: str):
        self.text = text
        s = text.strip()

        def name(i):
            if i < len(s) and s[i] == "<":
                j = s.index(">", i)
                return s[i + 1:j], j + 1
            j = i
            while j < len(s) and s[j].isalpha():
                j += 1
            if j - i < 3:
                raise ValueError(f"bad zone name in {text!r}")
            return s[i:j], j

        def offset(i, allow_empty=False):
            m = re.match(r"([+-]?)(\d{1,3})(?::(\d{1,2}))?(?::(\d{1,2}))?", s[i:])
            if not m:
                if allow_empty:
                    return None, i
                raise ValueError(f"bad offset in {text!r}")
            sign = -1 if m.group(1) == "-" else 1
            return sign * (int(m.group(2)) * 3600 + int(m.group(3) or 0) * 60 + int(m.group(4) or 0)), i + m.end()

        self.std_name, i = name(0)
        self.std_off, i = offset(i)
        self.has_dst = False
        if i < len(s) and s[i] != ",":
            self.has_dst = True
            self.dst_name, i = name(i)
            off, i = offset(i, allow_empty=True)
            self.dst_off = self.std_off - 3600 if off is None else off
            if i < len(s) and s[i] == ",":
                self.start, i = self._rule(s, i + 1)
                if i >= len(s) or s[i] != ",":
                    raise ValueError(f"missing end rule in {text!r}")
                self.end, i = self._rule(s, i + 1)
            else:  # no rule: US rules, as glibc assumes
                self.start = ("M", 3, 2, 0, 7200)
                self.end = ("M", 11, 1, 0, 7200)
        if i != len(s):
            raise ValueError(f"trailing text in {text!r}")

    @staticmethod
    def _rule(s, i):
        if s[i] == "M":
            m = re.match(r"M(\d{1,2})\.(\d)\.(\d)", s[i:])
            head = ("M", int(m.group(1)), int(m.group(2)), int(m.group(3)))
        elif s[i] == "J":
            m = re.match(r"J(\d{1,3})", s[i:])
            head = ("J", int(m.group(1)))
        else:
            m = re.match(r"(\d{1,3})", s[i:])
            head = ("N", int(m.group(1)))
        i += m.end()
        t = 7200
        if i < len(s) and s[i] == "/":
            m = re.match(r"([+-]?)(\d{1,3})(?::(\d{1,2}))?(?::(\d{1,2}))?", s[i + 1:])
            sign = -1 if m.group(1) == "-" else 1
            t = sign * (int(m.group(2)) * 3600 + int(m.group(3) or 0) * 60 + int(m.group(4) or 0))
            i += 1 + m.end()
        return head + (t,), i

    @staticmethod
    def _rule_day(year, rule) -> dt.date:
        if rule[0] == "M":
            _, month, week, wday, _t = rule
            first_wday = (dt.date(year, month, 1).weekday() + 1) % 7  # Sunday = 0
            day = 1 + (wday - first_wday) % 7 + (week - 1) * 7
            while day > calendar.monthrange(year, month)[1]:
                day -= 7
            return dt.date(year, month, day)
        if rule[0] == "J":  # 1..365, 29 Feb never counted
            d = dt.date(2001, 1, 1) + dt.timedelta(days=rule[1] - 1)
            return dt.date(year, d.month, d.day)
        return dt.date(year, 1, 1) + dt.timedelta(days=rule[1])  # 0..365, 29 Feb counted

    def offset_at_utc(self, t: float) -> int:
        """Seconds WEST of UTC in force at UTC epoch t."""
        if not self.has_dst:
            return self.std_off
        year = dt.datetime.fromtimestamp(t - self.std_off, dt.timezone.utc).year
        epoch = dt.datetime(1970, 1, 1)
        sd, ed = self._rule_day(year, self.start), self._rule_day(year, self.end)
        start = (dt.datetime(sd.year, sd.month, sd.day) - epoch).total_seconds() + self.start[-1] + self.std_off
        end = (dt.datetime(ed.year, ed.month, ed.day) - epoch).total_seconds() + self.end[-1] + self.dst_off
        in_dst = (start <= t < end) if start < end else not (end <= t < start)
        return self.dst_off if in_dst else self.std_off

    def local_to_utc(self, year, month, day, minutes) -> float:
        local = (dt.datetime(year, month, day) - dt.datetime(1970, 1, 1)).total_seconds() + minutes * 60
        t = local + self.std_off
        if self.offset_at_utc(t) != self.std_off:
            t = local + self.dst_off
        return t


# ------------------------------------------------------------------------------------------------------------
# Yearly file
# ------------------------------------------------------------------------------------------------------------
def year_path(key: str, year: int, out_root: pathlib.Path = OUT_ROOT) -> pathlib.Path:
    return out_root / key / f"{year}.json"


def render_year(key: str, year: int, tz_text: str, rows: list) -> str:
    head = {"v": 1, "location": key, "year": year, "tz": tz_text, "fields": FIELDS}
    body = ",\n".join(json.dumps([hhmm(v) for v in r], separators=(",", ":")) for r in rows)
    return json.dumps(head, separators=(",", ":"))[:-1] + ',"days":[\n' + body + "\n]}\n"


def read_year(path: pathlib.Path):
    """-> (header dict, rows as lists of 12 minute values)."""
    doc = json.loads(pathlib.Path(path).read_text())
    if doc.get("fields") != FIELDS:
        raise DataError(f"{path}: unexpected fields {doc.get('fields')}")
    rows = [[int(t[:2]) * 60 + int(t[3:]) for t in day] for day in doc["days"]]
    return doc, rows


def drift_warnings(key, year, rows, prev_rows, tz: PosixTZ, max_drift: int) -> list:
    """Adhan times that moved more than max_drift minutes from the same date last year (through UTC)."""
    out = []
    d = dt.date(year, 1, 1)
    while d.year == year:
        md = (d.month, 28 if (d.month == 2 and d.day == 29) else d.day)
        prev_doy = dt.date(year - 1, *md).timetuple().tm_yday
        for f in ADHAN_ORDER:
            i = FIELDS.index(f)
            cur = tz.local_to_utc(year, d.month, d.day, rows[d.timetuple().tm_yday - 1][i])
            prv = tz.local_to_utc(year - 1, md[0], md[1], prev_rows[prev_doy - 1][i])
            diff = ((cur - prv) / 60) % 1440
            if diff > 720:
                diff -= 1440
            if abs(diff) > max_drift:
                out.append(f"{key}/{year} {d.isoformat()}: {f} differs by {diff:+.0f} min from {year - 1}")
        d += dt.timedelta(days=1)
    return out


def write_year(key: str, year: int, rows: list, *, tz_text: str | None = None, warnings: list | None = None,
               out_root: pathlib.Path = OUT_ROOT, max_drift: int = 3, dry_run: bool = False,
               prev_rows: list | None = None) -> pathlib.Path:
    """Validate one year and write docs/athantimes/<key>/<year>.json. Prints a summary; raises DataError."""
    warnings = list(warnings or [])
    ndays = 366 if calendar.isleap(year) else 365
    if len(rows) != ndays:
        raise DataError(f"{key}/{year}: {len(rows)} days, expected {ndays}")
    if tz_text is None:
        tz_text = mosque(key)["tz"]
    tz = PosixTZ(tz_text)
    if prev_rows is None:
        prev = year_path(key, year - 1, out_root)
        if prev.exists():
            prev_rows = read_year(prev)[1]
    if prev_rows is not None:
        warnings += drift_warnings(key, year, rows, prev_rows, tz, max_drift)
    text = render_year(key, year, tz_text, rows)
    if len(text.encode()) > MAX_BYTES:
        raise DataError(f"{key}/{year}: {len(text)} bytes is over the device's {MAX_BYTES} byte limit")
    for w in warnings:
        print(f"  ! {w}")
    out = year_path(key, year, out_root)
    if not dry_run:
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(text)
    print(f"✓ {key}/{year}: {len(rows)} days, {len(text) / 1000:.1f} KB, {len(warnings)} warning(s)"
          + (" (dry run)" if dry_run else f" -> {out}"))
    return out
