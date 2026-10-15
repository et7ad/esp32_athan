#!/usr/bin/env python3
"""Check docs/, the data every clock downloads, against what the firmware and the docs promise. No network.

    firmware/tests/check_docs.py [--harness PATH]          (run_tests.sh runs it)

Errors (exit 1): what a clock would refuse or get wrong, and lists that disagree.
  - audio/catalog.json: at most CATALOG_MAX_BYTES (athan.cpp); the four lists, each entry a name and a url; names
    plain ASCII, 1 to 12 characters (the OLED fonts); no link twice in a list (the clock tells entries apart by
    their link); a relative link names a file under docs/audio/ that passes the slot's size and length limits
    (audio_slots.h) as MPEG Layer III.
  - radio/stations.json: exactly 10 entries, names as above, links http(s) or empty.
  - athantimes/mosques.json: keys and time zones as the clock takes them (scripts/athan_yearly.py, the rules of
    tz_posix.cpp), and the same keys in mosques.json, the Location options in firmware/athan.yaml, the table in
    README 1.7 and the folders under docs/athantimes/.
  - every athantimes/<key>/<year>.json: prayertimes_specs.md "What the clock checks", its tz equal to mosques.json.
  - with --harness (test_year_files, built by run_tests.sh): the clock's own parse_year_file() accepts every
    yearly file, and its parse_posix_tz() accepts exactly the time zones athan_yearly.PosixTZ accepts.
Warnings (printed): a sample rate other than the clock's I2S rate, stereo sounds, iqama before adhan, steps of
more than athan_yearly.MAX_DAY_JUMP minutes from one day to the next and changes of more than 3 minutes from last
year (both through UTC), Doha not at mosques.json's doha_after_sunrise, README 1.7 years that differ from the files.
"""

import argparse
import datetime as dt
import json
import pathlib
import random
import re
import subprocess
import sys

sys.dont_write_bytecode = True  # no __pycache__ in scripts/
REPO = pathlib.Path(__file__).resolve().parents[2]
DOCS = REPO / "docs"
COMP = REPO / "firmware" / "components" / "athan"
sys.path.insert(0, str(REPO / "scripts"))
import athan_yearly as ay  # noqa: E402

errors: list = []
warnings: list = []


def err(msg):
    errors.append(msg)


def warn(msg):
    warnings.append(msg)


def source(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8")


def cpp_number(text: str, name: str, where: str):
    """The value of `NAME = 64 * 1024;` (digits, *, integer suffixes) in a C++ source, or None (an error)."""
    m = re.search(rf"\b{name}\s*=\s*([0-9xXa-fA-F*\s]+?)(?:[uUlL]*)\s*;", text)
    if not m:
        err(f"cannot read {name} from {where}")
        return None
    value = 1
    for part in m.group(1).split("*"):
        value *= int(part.strip(), 0)
    return value


def cpp_list(text: str, name: str, where: str):
    """The items of `NAME[...] = {a, b, ...};` in a C++ source, as strings, or None (an error)."""
    m = re.search(rf"\b{name}\[[^\]]*\]\s*=\s*\{{([^}}]*)\}}", text)
    if not m:
        err(f"cannot read {name} from {where}")
        return None
    return [x.strip().strip('"') for x in m.group(1).split(",") if x.strip()]


def is_name(s) -> bool:
    return isinstance(s, str) and 1 <= len(s) <= 12 and s.isascii() and s.isprintable()


# ----------------------------------------------------------------------------------------------------------------
# MPEG Layer III frames, as the clock's mp3_scan() reads them: an ID3v2 tag skipped, three chained frames to sync,
# the duration the sum of the frames.
# ----------------------------------------------------------------------------------------------------------------
_BR_V1 = [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320]
_BR_V2 = [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160]
_SR = {3: (44100, 48000, 32000), 2: (22050, 24000, 16000), 0: (11025, 12000, 8000)}


def _frame(data: bytes, p: int):
    if p + 4 > len(data) or data[p] != 0xFF or (data[p + 1] & 0xE0) != 0xE0:
        return None
    ver, layer = (data[p + 1] >> 3) & 3, (data[p + 1] >> 1) & 3
    bri, sri, pad = data[p + 2] >> 4, (data[p + 2] >> 2) & 3, (data[p + 2] >> 1) & 1
    if ver == 1 or layer != 1 or bri in (0, 15) or sri == 3:
        return None
    rate = _SR[ver][sri]
    spf = 1152 if ver == 3 else 576
    length = (spf // 8) * (_BR_V1 if ver == 3 else _BR_V2)[bri] * 1000 // rate + pad
    return length, rate, 1 if data[p + 3] >> 6 == 3 else 2, spf


def mp3_info(data: bytes):
    """(sample rate, channels, duration ms) or an error string."""
    start = 0
    if data[:3] == b"ID3" and len(data) >= 10:
        start = 10 + ((data[6] & 0x7F) << 21 | (data[7] & 0x7F) << 14 | (data[8] & 0x7F) << 7 | (data[9] & 0x7F))
        start += 10 if data[5] & 0x10 else 0
    for p in range(start, min(len(data) - 4, start + 65536)):
        f1 = _frame(data, p)
        if not f1:
            continue
        f2 = _frame(data, p + f1[0])
        f3 = f2 and _frame(data, p + f1[0] + f2[0])
        if not (f3 and f1[1] == f2[1] == f3[1]):
            continue
        frames, samples, q = 0, 0, p
        while (f := _frame(data, q)) and f[1] == f1[1]:
            frames += 1
            samples += f[3]
            q += f[0]
        return f1[1], f1[2], samples * 1000 // f1[1]
    return "no MPEG Layer III frames"


# ----------------------------------------------------------------------------------------------------------------
def check_catalog(data_url: str, i2s_rate):
    path = DOCS / "audio" / "catalog.json"
    athan_cpp = source(COMP / "athan.cpp")
    slots_h = source(COMP / "audio_slots.h")
    limit = cpp_number(athan_cpp, "CATALOG_MAX_BYTES", "athan.cpp")
    names = cpp_list(slots_h, "SLOT_NAMES", "audio_slots.h")
    max_bytes = cpp_list(slots_h, "SLOT_MAX_BYTES", "audio_slots.h")
    max_ms = cpp_list(slots_h, "SLOT_MAX_MS", "audio_slots.h")
    if None in (limit, names, max_bytes, max_ms):
        return "catalog not checked"
    raw = path.read_bytes()
    if len(raw) > limit:
        err(f"audio/catalog.json: {len(raw)} bytes, the clock reads at most {limit}")
    try:
        doc = json.loads(raw)
    except ValueError as e:
        err(f"audio/catalog.json: not JSON ({e})")
        return "catalog not readable"
    total, rates, stereo = 0, {}, []
    for slot, lst in enumerate(names):
        entries = doc.get(lst) if isinstance(doc, dict) else None
        if not isinstance(entries, list) or not entries:
            err(f"audio/catalog.json: list {lst!r} missing or empty (entry 1 is the default a new clock downloads)")
            continue
        seen = {}
        for n, e in enumerate(entries, 1):
            total += 1
            where = f"audio/catalog.json {lst} entry {n}"
            if not isinstance(e, dict) or not isinstance(e.get("name"), str) or not isinstance(e.get("url"), str):
                err(f"{where}: needs a name and a url (the clock skips it, and every later number moves up)")
                continue
            if not is_name(e["name"]):
                err(f"{where}: name {e['name']!r} must be plain ASCII, 1 to 12 characters")
            url = e["url"]
            full = url if url.startswith(("http://", "https://")) else \
                data_url + url if url.startswith("/") else f"{data_url}/audio/{url}"
            if full in seen:
                err(f"{where}: same link as entry {seen[full]} ({url})")
            seen[full] = n
            if not url:
                err(f"{where}: empty url")
                continue
            if not full.startswith(data_url + "/"):
                warn(f"{where}: {url} is not in this repository, not checked offline")
                continue
            local = DOCS / full[len(data_url) + 1:]
            if not local.is_file():
                err(f"{where}: {url} -> {local.relative_to(REPO)} does not exist")
                continue
            body = local.read_bytes()
            if len(body) > int(max_bytes[slot]):
                err(f"{where}: {local.relative_to(REPO)} is {len(body)} bytes, the {lst} slot takes {max_bytes[slot]}")
            info = mp3_info(body)
            if isinstance(info, str):
                err(f"{where}: {local.relative_to(REPO)}: {info}")
                continue
            rate, channels, ms = info
            if ms > int(max_ms[slot]):
                err(f"{where}: {local.relative_to(REPO)} lasts {ms} ms, the {lst} slot takes {max_ms[slot]} ms")
            rates[rate] = rates.get(rate, 0) + 1
            if channels != 1:
                stereo.append(str(local.relative_to(DOCS / "audio")))
    if i2s_rate:
        other = sum(c for r, c in rates.items() if r != i2s_rate)
        if other:
            warn(f"audio/catalog.json: {other} of {sum(rates.values())} files are "
                 f"{', '.join(f'{r} Hz' for r in sorted(rates) if r != i2s_rate)}; the clock's I2S runs at {i2s_rate} Hz "
                 "(athan.yaml), so they are resampled on the clock")
    if stereo:
        warn(f"audio/catalog.json: stereo files play their left channel only: {', '.join(stereo)}")
    return f"catalog {total} entries, {len(raw) / 1000:.1f} KB"


def check_stations():
    path = DOCS / "radio" / "stations.json"
    limit = cpp_number(source(COMP / "athan.cpp"), "STATIONS_MAX_BYTES", "athan.cpp")
    raw = path.read_bytes()
    if limit and len(raw) > limit:
        err(f"radio/stations.json: {len(raw)} bytes, the clock reads at most {limit}")
    try:
        stations = json.loads(raw).get("stations")
    except (ValueError, AttributeError) as e:
        err(f"radio/stations.json: not readable ({e})")
        return "stations not readable"
    if not isinstance(stations, list) or len(stations) != 10:
        err(f"radio/stations.json: {len(stations) if isinstance(stations, list) else 'no'} stations, exactly 10 needed")
        return "stations"
    urls = {}
    for n, s in enumerate(stations, 1):
        where = f"radio/stations.json station {n}"
        if not isinstance(s, dict) or not isinstance(s.get("name"), str) or not isinstance(s.get("url"), str):
            err(f"{where}: needs a name and a url")
            continue
        if s["url"] and not s["url"].startswith(("http://", "https://")):
            err(f"{where}: link {s['url']!r} is not http:// or https://")
        if not (is_name(s["name"]) or (s["name"] == "" and s["url"] == "")):
            err(f"{where}: name {s['name']!r} must be plain ASCII, 1 to 12 characters")
        if s["url"] in urls:
            warn(f"{where}: same link as station {urls[s['url']]}")
        if s["url"]:
            urls[s["url"]] = n
    return "stations 10"


def check_keys(mosques: dict):
    yaml = source(REPO / "firmware" / "athan.yaml")
    m = re.search(r'name:\s*"Location"(?:\n[^\n]*){0,6}?\n\s*options:\s*\[([^\]]*)\]', yaml)
    options = [x.strip().strip("\"'") for x in m.group(1).split(",")] if m else None
    if options is None:
        err("cannot read the Location select's options from firmware/athan.yaml")
    readme = source(REPO / "README.md")
    m = re.search(r"Mosques available now:\s*\n\s*\n((?:\|[^\n]*\n)+)", readme)
    table = {}
    if m:
        for row in m.group(1).splitlines():
            cells = [c.strip() for c in row.strip().strip("|").split("|")]
            k = re.fullmatch(r"`([^`]+)`", cells[0]) if cells else None
            if k and len(cells) >= 3:
                table[k.group(1)] = cells[2]
    else:
        err("cannot find the mosque table of README 1.7 (Mosques available now:)")
    folders = {p.name for p in (DOCS / "athantimes").iterdir() if p.is_dir()}
    sets = {"mosques.json": set(mosques), "the Location options (athan.yaml)": set(options or []),
            "README 1.7": set(table), "docs/athantimes folders": folders}
    every = set().union(*sets.values())
    for k in sorted(every):
        missing = [name for name, s in sets.items() if k not in s and (name != "the Location options (athan.yaml)"
                                                                         or options is not None)]
        if missing:
            err(f"mosque key {k!r} is missing from {', '.join(missing)}")
    if options is not None and len(options) != len(set(options)):
        err("the Location select lists a key twice")
    for k, years in table.items():
        files = sorted(int(p.stem) for p in (DOCS / "athantimes" / k).glob("*.json") if re.fullmatch(r"\d{4}", p.stem))
        r = re.fullmatch(r"(\d{4})\s*[–-]\s*(\d{4})|(\d{4})", years)
        if files and r:
            lo, hi = (int(r.group(1)), int(r.group(2))) if r.group(1) else (int(r.group(3)), int(r.group(3)))
            if (lo, hi) != (files[0], files[-1]):
                warn(f"README 1.7 says {k}: {years}, the files are {files[0]}–{files[-1]}")
    return f"{len(mosques)} mosques"


def check_registry():
    reg = json.loads((DOCS / "athantimes" / "mosques.json").read_text())
    mosques = reg.get("mosques", {})
    for k, m in mosques.items():
        try:
            ay.check_key(k)
            ay.check_tz(k, m.get("tz"))
        except ay.DataError as e:
            err(f"athantimes/mosques.json: {e}")
        d = m.get("doha_after_sunrise")
        if not isinstance(d, int) or d < 1:
            warn(f"athantimes/mosques.json {k}: doha_after_sunrise {d!r} (the scripts need minutes > 0 when a "
                 "source has no Doha)")
    default = reg.get("iqama_after_default")
    if not isinstance(default, dict) or set(default) != set(ay.DEFAULT_IQAMA_AFTER):
        err("athantimes/mosques.json: iqama_after_default needs fajr, dhuhar, asr, maghrib and isha")
    elif default != ay.DEFAULT_IQAMA_AFTER:
        warn(f"athantimes/mosques.json iqama_after_default {default} differs from athan_yearly.DEFAULT_IQAMA_AFTER "
             f"{ay.DEFAULT_IQAMA_AFTER} (santaclara_prayer_times_parser.py and make_yearly_json.py use the latter)")
    return mosques


def check_years(mosques: dict):
    limit = cpp_number(source(COMP / "prayer_store.h"), "MAX_YEAR_FILE_BYTES", "prayer_store.h") or 100000
    strict = re.compile(r"([01][0-9]|2[0-3]):[0-5][0-9]")
    adhan_cols = [ay.FIELDS.index(f) for f in ay.ADHAN_ORDER]
    tables, count = {}, 0
    for kdir in sorted(p for p in (DOCS / "athantimes").iterdir() if p.is_dir()):
        key = kdir.name
        for f in sorted(kdir.glob("*.json")):
            where = f"athantimes/{key}/{f.name}"
            if not re.fullmatch(r"\d{4}", f.stem):
                err(f"{where}: not a <year>.json name; clocks only ask for athantimes/<key>/<year>.json")
                continue
            count += 1
            year, raw = int(f.stem), f.read_bytes()
            if len(raw) > limit:
                err(f"{where}: {len(raw)} bytes, the clock refuses more than {limit}")
            try:
                doc = json.loads(raw)
            except ValueError as e:
                err(f"{where}: not JSON ({e})")
                continue
            if not isinstance(doc, dict):
                err(f"{where}: not a JSON object")
                continue
            if doc.get("location") != key:
                err(f"{where}: location {doc.get('location')!r}, the clock asks for {key!r}")
            if type(doc.get("year")) is not int or doc["year"] != year:
                err(f"{where}: year {doc.get('year')!r}, the file name says {year}")
            if doc.get("v") != 1:
                warn(f"{where}: v {doc.get('v')!r} (the scripts write 1)")
            try:
                tz = ay.check_tz(key, doc.get("tz"))
            except ay.DataError as e:
                err(f"{where}: {e}")
                continue
            if key in mosques and doc.get("tz") != mosques[key].get("tz"):
                err(f"{where}: tz {doc.get('tz')!r}, mosques.json says {mosques[key].get('tz')!r}")
            fields = doc.get("fields")
            if not isinstance(fields, list) or any(x not in fields for x in ay.FIELDS):
                err(f"{where}: fields must name all 12 of {ay.FIELDS}")
                continue
            col = [fields.index(x) for x in ay.FIELDS]
            if fields != ay.FIELDS:
                warn(f"{where}: fields in another order than the scripts write")
            days = doc.get("days")
            want = 366 if dt.date(year, 12, 31).timetuple().tm_yday == 366 else 365
            if not isinstance(days, list) or len(days) != want:
                err(f"{where}: {len(days) if isinstance(days, list) else 'no'} days, {year} has {want}")
                continue
            rows, bad = [], 0
            for i, day in enumerate(days):
                d = dt.date(year, 1, 1) + dt.timedelta(days=i)
                if not isinstance(day, list) or len(day) < len(fields) or \
                        any(not isinstance(day[c], str) or not strict.fullmatch(day[c]) for c in col):
                    bad += 1
                    if bad <= 3:
                        err(f"{where} {d}: not 12 strict HH:MM times: {day!r}")
                    continue
                row = [int(day[c][:2]) * 60 + int(day[c][3:]) for c in col]
                seq = [row[c] for c in adhan_cols]
                if any(a >= b for a, b in zip(seq, seq[1:])):
                    err(f"{where} {d}: adhan times out of order: {' '.join(ay.hhmm(v) for v in seq)}")
                rows.append(row)
            if bad > 3:
                err(f"{where}: {bad} days with bad times in all")
            if len(rows) != want:
                continue
            tables[(key, year)] = (rows, tz)
            iqama = [f"{dt.date(year, 1, 1) + dt.timedelta(days=i)} {iq}" for i, r in enumerate(rows)
                     for iq, ad in ay.IQAMA_OF.items() if r[ay.FIELDS.index(iq)] < r[ay.FIELDS.index(ad)]]
            if iqama:
                warn(f"{where}: {len(iqama)} iqama time(s) before the adhan: {', '.join(iqama[:4])}"
                     + (" ..." if len(iqama) > 4 else ""))
            doha = mosques.get(key, {}).get("doha_after_sunrise")
            if isinstance(doha, int):
                off = sum(1 for r in rows if r[ay.FIELDS.index("doha")] - r[ay.FIELDS.index("sunrise")] != doha)
                if off:
                    warn(f"{where}: Doha is not sunrise + {doha} (mosques.json) on {off} day(s)")
    for (key, year), (rows, tz) in sorted(tables.items()):
        prev = tables.get((key, year - 1), (None, None))[0]
        for j in ay.continuity_warnings(year, rows, tz, ay.MAX_DAY_JUMP, prev):
            warn(f"athantimes/{key}/{year}.json {j}")
        if prev:
            drift = {}
            for w in ay.drift_warnings(key, year, rows, prev, tz, 3):
                m = re.search(r" (\d{4}-\d\d-\d\d): (\w+) differs by ([+-]\d+) min", w)
                drift.setdefault(m.group(2), []).append((m.group(1), int(m.group(3))))
            for field, hits in sorted(drift.items()):
                steps = sorted({s for _, s in hits})
                warn(f"athantimes/{key}/{year}.json: {field} differs by more than 3 min from {year - 1} on {len(hits)} "
                     f"day(s) ({steps[0]:+d} to {steps[-1]:+d} min), first {hits[0][0]}")
    return f"{count} yearly files", tables


def check_harness(harness: str, mosques: dict):
    files = sorted(str(p) for p in (DOCS / "athantimes").glob("*/*.json") if re.fullmatch(r"\d{4}", p.stem))
    r = subprocess.run([harness] + files, capture_output=True, text=True)
    for line in r.stdout.splitlines():
        if line.startswith("BAD"):
            err(f"the clock's parse_year_file() refuses {line[4:]}")
    ok = sum(1 for line in r.stdout.splitlines() if line.startswith("OK"))
    # The scripts' time zone parser and the clock's: the same strings accepted and refused.
    cases = [m.get("tz", "") for m in mosques.values()] + [
        "PST8PDT,M3.2.0,M11.1.0", "EST5EDT", "<+03>-3", "IST-5:30", "EET-2EEST,M4.5.5/0,M10.5.4/24",
        "CET-1CEST,M3.5.0,M10.5.0/3", "<-03>3<-02>,M3.5.0/-2,M10.5.0/-1", "PST8PDT,J60/1:30:15,J300", "PST8PDT,59,300",
        "PST8PDT,M3.2.7,M11.1.0", "PST8PDT,M13.2.0,M11.1.0", "PST8PDT,M3.6.0,M11.1.0", "PST8PDT,M3.0.0,M11.1.0",
        "PST8PDT,J366,M11.1.0", "PST8PDT,J0,M11.1.0", "PST8PDT,366,0", "<>3", "PST8PDT,M3.2.0", "PST8",
        "America/Los_Angeles", " PST8PDT,M3.2.0,M11.1.0 ", "PST8PDT,M3.2.0/,M11.1.0", "PST1234", "PS8"]
    rnd = random.Random(7)
    for _ in range(400):
        cases.append("".join(rnd.choice("PSTDM0123456789.,/:+-<>J ") for _ in range(rnd.randint(1, 18))))
        cases.append(f"PST8PDT,M{rnd.randint(0, 14)}.{rnd.randint(0, 6)}.{rnd.randint(0, 8)},M11.1.0")
    out = subprocess.run([harness] + [f"tz:{c}" for c in cases], capture_output=True, text=True).stdout
    device = re.findall(r"(accepted|REFUSED)\n", out)
    if len(device) != len(cases):
        err(f"test_year_files answered {len(device)} of {len(cases)} time zones")
        return f"clock checks: {ok} of {len(files)} files accepted"
    differ = []
    for c, d in zip(cases, device):
        try:
            ay.PosixTZ(c)
            script = True
        except ValueError:
            script = False
        if script != (d == "accepted"):
            differ.append(repr(c))
    if differ:
        err(f"time zones the scripts and the clock judge differently: {', '.join(differ[:5])}")
    return f"clock checks: {ok} of {len(files)} files accepted, {len(cases)} time zones judged alike"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--harness", help="test_year_files binary (run_tests.sh builds it when ArduinoJson is here)")
    a = ap.parse_args()
    yaml = source(REPO / "firmware" / "athan.yaml")
    m = re.search(r'^\s*data_url:\s*"?([^"\s#]+)', yaml, re.M)
    data_url = m.group(1).rstrip("/") if m else "https://raw.githubusercontent.com/et7ad/esp32_athan/main/docs"
    m = re.search(r"platform:\s*i2s_audio\b[^\n]*\n(?:[ \t]+[^\n]*\n){0,15}?[ \t]+sample_rate:\s*(\d+)", yaml)
    i2s_rate = int(m.group(1)) if m else None
    if i2s_rate is None:
        warn("cannot read the I2S speaker's sample_rate from firmware/athan.yaml: sample rates not compared")
    done = [check_catalog(data_url, i2s_rate), check_stations()]
    mosques = check_registry()
    done.append(check_keys(mosques))
    years, _ = check_years(mosques)
    done.append(years)
    if a.harness:
        done.append(check_harness(a.harness, mosques))
    for w in warnings:
        print(f"  warning: {w}")
    for e in errors:
        print(f"  ERROR: {e}")
    print(f"docs: {'; '.join(done)}: {len(errors)} error(s), {len(warnings)} warning(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
