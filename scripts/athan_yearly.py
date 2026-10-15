"""Shared code for every script that produces prayer-time data in this repository.

Every producer writes exactly ONE file per mosque per year:

    docs/athantimes/<key>/<year>.json

in the positional format of prayertimes_specs.md: a small header plus a "days" list with one list of 12
"HH:MM" strings per day (index 0 = 1 January), in the order of FIELDS. No per-day files, no per-day keys.

Use from a script in this folder:

    from athan_yearly import day_from_times, write_year, mosque
    rows = [day_from_times({...}, where=..., warnings=w, doha_after_sunrise=..., ...) for each day]
    write_year(key, year, rows)              # validates, compares with the previous year, writes one file

write_year() refuses what the clock would refuse (prayer_store.cpp parse_year_file and write: adhan order, day
count, size, the key, the time zone by tz_posix.cpp's rules) and warns about the rest: iqama before adhan, a change
of more than --max-drift minutes from the same date last year, and a step of more than MAX_DAY_JUMP minutes from
the day before (both through UTC).

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
KEY_RE = re.compile(r"[a-z0-9_]{1,31}")  # the clock stores at most 31 characters (prayer_store.cpp, the Location key)
MAX_TZ_CHARS = 63  # prayer_store.cpp keeps the time zone in 64 bytes
MAX_DAY_JUMP = 5  # minutes; real adhan times move at most about 3 minutes from one day to the next

_TIME_RE = re.compile(r"^\s*(\d{1,2}):(\d{2})\s*(AM|PM|am|pm)?\s*$")
_STRICT_RE = re.compile(r"^([01][0-9]|2[0-3]):[0-5][0-9]$")


class DataError(ValueError):
    """A problem that stops a year from being written."""


class LooseTime(str):
    """A time written as H:MM or with spaces: accepted. write_year() sums these up in one line per file."""


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
            warnings.append(LooseTime(f"{where}: {field} = {raw!r}"))
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
# POSIX TZ ("PST8PDT,M3.2.0,M11.1.0"), parsed by the rules of firmware/components/athan/tz_posix.cpp
# (parse_posix_tz): a string the clock refuses raises ValueError here too, so no script writes a year the clock
# would refuse as "bad time zone". Offsets are seconds WEST of UTC, as in POSIX.
# ------------------------------------------------------------------------------------------------------------
_DIGITS = "0123456789"


class PosixTZ:
    def __init__(self, text: str):
        self.text = text
        s, n = text, len(text)

        def name(i):
            # 3 or more ASCII letters, or at least one character between < and >
            if i < n and s[i] == "<":
                j = s.find(">", i)
                if j < 0 or j - i < 2:
                    raise ValueError(f"bad <...> zone name in {text!r}")
                return s[i + 1:j], j + 1
            j = i
            while j < n and s[j].isascii() and s[j].isalpha():
                j += 1
            if j - i < 3:
                raise ValueError(f"zone name in {text!r} needs 3 or more letters, or <...>")
            return s[i:j], j

        def hms(i):
            # [+|-]hh[:mm[:ss]], up to 3 digits per part -> seconds (sign as written); (None, i) when no number
            sign = 1
            if i < n and s[i] in "+-":
                sign = -1 if s[i] == "-" else 1
                i += 1
            if i >= n or s[i] not in _DIGITS:
                return None, i
            parts = [0, 0, 0]
            for k in range(3):
                j = i
                while j < n and s[j] in _DIGITS and j - i < 3:
                    j += 1
                parts[k] = int(s[i:j])
                i = j
                if k < 2 and i + 1 < n and s[i] == ":" and s[i + 1] in _DIGITS:
                    i += 1
                    continue
                break
            return sign * (parts[0] * 3600 + parts[1] * 60 + parts[2]), i

        def number(i):
            j = i
            while j < n and s[j] in _DIGITS:
                j += 1
            if j == i:
                raise ValueError(f"bad DST rule in {text!r}")
            return int(s[i:j]), j

        def rule(i):
            if i < n and s[i] == "M":
                month, i = number(i + 1)
                if i >= n or s[i] != ".":
                    raise ValueError(f"bad DST rule in {text!r} (Mmonth.week.weekday)")
                week, i = number(i + 1)
                if i >= n or s[i] != ".":
                    raise ValueError(f"bad DST rule in {text!r} (Mmonth.week.weekday)")
                wday, i = number(i + 1)
                if not (1 <= month <= 12 and 1 <= week <= 5 and wday <= 6):
                    raise ValueError(f"DST rule M{month}.{week}.{wday} in {text!r}: month 1-12, week 1-5, "
                                     "weekday 0-6")
                head = ("M", month, week, wday)
            elif i < n and s[i] == "J":
                day, i = number(i + 1)
                if not 1 <= day <= 365:
                    raise ValueError(f"DST rule J{day} in {text!r}: J1 to J365")
                head = ("J", day)
            else:
                day, i = number(i)
                if day > 365:
                    raise ValueError(f"DST rule {day} in {text!r}: 0 to 365")
                head = ("N", day)
            t = 7200
            if i < n and s[i] == "/":
                t, i = hms(i + 1)
                if t is None:
                    raise ValueError(f"bad DST rule time in {text!r}")
            return head + (t,), i

        i = 0
        while i < n and s[i] == " ":
            i += 1
        self.std_name, i = name(i)
        self.std_off, i = hms(i)
        if self.std_off is None:
            raise ValueError(f"bad offset in {text!r}")
        self.has_dst = False
        self.dst_off = self.std_off
        if i < n and s[i] != ",":
            self.dst_name, i = name(i)
            self.has_dst = True
            self.dst_off = self.std_off - 3600
            if i < n and s[i] != ",":
                self.dst_off, i = hms(i)
                if self.dst_off is None:
                    raise ValueError(f"bad daylight-saving offset in {text!r}")
            if i < n and s[i] == ",":
                self.start, i = rule(i + 1)
                if i >= n or s[i] != ",":
                    raise ValueError(f"missing end rule in {text!r}")
                self.end, i = rule(i + 1)
            else:  # no rule: US rules, as glibc assumes
                self.start = ("M", 3, 2, 0, 7200)
                self.end = ("M", 11, 1, 0, 7200)
        while i < n and s[i] == " ":
            i += 1
        if i != n:
            raise ValueError(f"trailing text in {text!r}")

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


def continuity_warnings(year, rows, tz: PosixTZ, max_jump: int = MAX_DAY_JUMP, prev_rows: list | None = None) -> list:
    """Adhan times that move more than max_jump minutes from the day before, compared through UTC so that DST
    changes don't count. Real times move at most about 3 minutes a day, so a bigger step is a typo or a DST mistake
    in the source; unlike the year-to-year check it also finds a typo that last year's file has too.
    prev_rows (last year) adds the step from 31 December to 1 January."""
    days = [(dt.date(year, 1, 1) + dt.timedelta(days=i), r) for i, r in enumerate(rows)]
    if prev_rows:
        days.insert(0, (dt.date(year - 1, 12, 31), prev_rows[-1]))
    out = []
    for (d0, r0), (d1, r1) in zip(days, days[1:]):
        for f in ADHAN_ORDER:
            i = FIELDS.index(f)
            step = (tz.local_to_utc(d1.year, d1.month, d1.day, r1[i])
                    - tz.local_to_utc(d0.year, d0.month, d0.day, r0[i])) / 60 - 1440
            if abs(step) > max_jump:
                out.append(f"{d1.isoformat()} {f} {hhmm(r1[i])}: {step:+.0f} min from {hhmm(r0[i])} the day before")
    return out


def check_key(key: str) -> None:
    """The mosque key as the clock stores it (prayer_store.cpp, the Location setting); raises DataError."""
    if not isinstance(key, str) or not KEY_RE.fullmatch(key):
        raise DataError(f"mosque key {key!r}: the clock takes 1 to 31 characters of a-z, 0-9 and _")


def check_tz(key: str, tz_text: str) -> PosixTZ:
    """The time zone as the clock takes it (prayer_store.cpp, tz_posix.cpp); raises DataError."""
    if not isinstance(tz_text, str) or len(tz_text) > MAX_TZ_CHARS:
        raise DataError(f"{key}: time zone {tz_text!r} is not a string of at most {MAX_TZ_CHARS} characters")
    try:
        return PosixTZ(tz_text)
    except ValueError as e:
        raise DataError(f"{key}: the clock would refuse the time zone: {e}") from None


def write_year(key: str, year: int, rows: list, *, tz_text: str | None = None, warnings: list | None = None,
               out_root: pathlib.Path = OUT_ROOT, max_drift: int = 3, dry_run: bool = False,
               prev_rows: list | None = None, max_jump: int = MAX_DAY_JUMP) -> pathlib.Path:
    """Validate one year and write docs/athantimes/<key>/<year>.json. Prints a summary; raises DataError."""
    warnings = list(warnings or [])
    loose = [w for w in warnings if isinstance(w, LooseTime)]
    warnings = [w for w in warnings if not isinstance(w, LooseTime)]
    if loose:  # one line per file, not one per value (a printed 12-hour timetable writes most hours as H:MM)
        warnings.insert(0, f"{len(loose)} time(s) not written as HH:MM, accepted and stored as HH:MM "
                           f"(first: {loose[0]})")
    ndays = 366 if calendar.isleap(year) else 365
    if len(rows) != ndays:
        raise DataError(f"{key}/{year}: {len(rows)} days, expected {ndays}")
    check_key(key)
    if tz_text is None:
        tz_text = mosque(key)["tz"]
    tz = check_tz(key, tz_text)
    if prev_rows is None:
        prev = year_path(key, year - 1, out_root)
        if prev.exists():
            prev_rows = read_year(prev)[1]
    if prev_rows is not None:
        warnings += drift_warnings(key, year, rows, prev_rows, tz, max_drift)
    jumps = continuity_warnings(year, rows, tz, max_jump, prev_rows)
    text = render_year(key, year, tz_text, rows)
    size = len(text.encode())
    if size > MAX_BYTES:
        raise DataError(f"{key}/{year}: {size} bytes is over the device's {MAX_BYTES} byte limit")
    for w in warnings:
        print(f"  ! {w}")
    if jumps:
        print(f"  ! WARNING {key}/{year}: {len(jumps)} day-to-day step(s) of more than {max_jump} min (through UTC; "
              "real times move at most about 3 min a day). Check these days in the source:")
        for j in jumps:
            print(f"  !   {j}")
    out = year_path(key, year, out_root)
    if not dry_run:
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(text)
    print(f"✓ {key}/{year}: {len(rows)} days, {size / 1000:.1f} KB, {len(warnings) + len(jumps)} warning(s)"
          + (" (dry run)" if dry_run else f" -> {out}"))
    return out
