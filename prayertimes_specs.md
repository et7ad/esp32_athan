# Prayer-time file format (version 3)

Version 3 clocks read **one file per mosque per year**:

```
docs/athantimes/<key>/<year>.json
https://raw.githubusercontent.com/et7ad/esp32_athan/main/docs/athantimes/<key>/<year>.json
```

Every script in `scripts/` writes exactly this file (through `scripts/athan_yearly.py`). The firmware parses it in
`firmware/components/athan/prayer_store.cpp` (`parse_year_file`). Change both together.

> V2 clocks (ESP8266, [et7ad/esp_athan](https://github.com/et7ad/esp_athan)) use a different format: one file
> per day with named keys and 12-hour afternoon times. That format lives only in the V2 repository and is not
> produced here.

## Layout

```json
{"v":1,"location":"davis","year":2026,"tz":"PST8PDT,M3.2.0,M11.1.0",
 "fields":["fajr","fajr_iqa","sunrise","doha","dhuhar","dhuhar_iqa","asr","asr_iqa","maghrib","maghrib_iqa","isha","isha_iqa"],
 "days":[
["06:06","06:31","07:24","07:44","12:14","12:24","14:39","14:49","16:57","17:12","18:16","18:31"],
["06:06","06:31","07:24","07:44","12:14","13:15","14:40","14:50","16:58","17:13","18:17","18:32"],
...
]}
```

| Key | Type | Rule |
|---|---|---|
| `v` | number | Format version, `1`. Not checked by the firmware today; raise it only together with a firmware change |
| `location` | string | The mosque key, equal to the folder name and to an option of the Location select in `firmware/athan.yaml`. At most 31 characters (`[a-z0-9_]` by convention). The clock refuses a file whose `location` is not the key it asked for |
| `year` | number | Gregorian year, equal to the file name. Refused if different |
| `tz` | string | POSIX time zone of the mosque, for example `PST8PDT,M3.2.0,M11.1.0` or `<+03>-3`. **Not** `America/Los_Angeles`. At most 63 characters. The clock sets its clock zone from it and computes DST offline |
| `fields` | list of 12 strings | Column names. All 12 below must be present; the firmware looks columns up by name, but the scripts always write this order |
| `days` | list of lists | One entry per day, `days[0]` = 1 January. Exactly 365 entries, or 366 in a leap year. Each entry is a list of 12 strings in the order of `fields` |

The 12 fields, in the order the scripts write them:

| # | Field | Meaning | Used by the clock |
|---|---|---|---|
| 0 | `fajr` | Fajr adhan | yes: Fajr athan; Pre-Fajr Tawashih 25 min before |
| 1 | `fajr_iqa` | Fajr iqama | stored only |
| 2 | `sunrise` | Sunrise | yes: shown as a "next prayer", no athan |
| 3 | `doha` | Doha (Duha) | yes: shown, no athan |
| 4 | `dhuhar` | Dhuhr adhan | yes |
| 5 | `dhuhar_iqa` | Dhuhr iqama | stored only |
| 6 | `asr` | Asr adhan | yes |
| 7 | `asr_iqa` | Asr iqama | stored only |
| 8 | `maghrib` | Maghrib adhan | yes |
| 9 | `maghrib_iqa` | Maghrib iqama | stored only |
| 10 | `isha` | Isha adhan | yes |
| 11 | `isha_iqa` | Isha iqama | stored only |

## Time values

- Strings `"HH:MM"`: exactly five characters, two-digit hour `00`–`23`, colon, two-digit minute `00`–`59`. No
  spaces, no seconds, no AM/PM.
- **True 24-hour local time:** Asr is `"14:39"`, never `"02:39"`. The V2 daily files wrote afternoon hours in
  12-hour form and the V2 firmware added 12 hours on the device. Version 3 does that once, in the scripts
  (`athan_yearly.PM_BELOW`): Fajr, sunrise and Doha as written; Dhuhr +12 when the hour is below 9; Asr,
  Maghrib and Isha +12 when below 12; each iqama like its prayer. A source with explicit AM/PM needs none of this.
- Local wall-clock time of the mosque, including daylight saving on DST days.

## What the clock checks (a failing file is refused and the stored year is kept)

1. The file is at most **100,000 bytes** (a full year with 12 fields is about 36 KB).
2. It is valid JSON with `location`, `year` and `tz` matching and a parseable `tz`.
3. All 12 field names are present.
4. `days` has 365 or 366 entries matching the year; every value is a valid `HH:MM`.
5. On every day the seven schedule times are strictly increasing: Fajr < sunrise < Doha < Dhuhr < Asr < Maghrib <
   Isha. Iqama values are not checked (nothing is scheduled by them).

## What the scripts check on top (warnings, the file is still written)

- An iqama earlier than its adhan (some timetables print a fixed Dhuhr iqama that the adhan passes in summer).
- An adhan more than 3 minutes away from the same date of the previous year, compared through UTC so that DST
  shifts don't count. Real years differ by about a minute, so this catches typos the ordering check misses.
  `--max-drift` changes the threshold.

A script **refuses** to write a year whose adhan times are out of order or that would not pass the clock's checks.

## Filling gaps

When a source has no Doha or no iqama column, the scripts derive them from
[docs/athantimes/mosques.json](docs/athantimes/mosques.json):

- `doha` = `sunrise` + the mosque's `doha_after_sunrise` (minutes).
- `<prayer>_iqa` = adhan + `iqama_after` of the mosque, or `iqama_after_default`
  (Fajr 30, Dhuhr 10, Asr 10, Maghrib 10, Isha 20).

Derived iqama times are estimates, not the mosque's official times.

## Publishing

- A file can appear any time before or after 1 January. Clocks look for next year's file daily from 1 December
  and every 6 hours from 1 January, and use last year's times (converted through the time zone) until it exists.
  A missing file is normal and not an error.
- A published file can be corrected in place. Clocks only re-download a stored year when the owner presses
  **Refresh Prayer Times**, so announce corrections.
- Never rename a key: clocks store the key and would stop finding their mosque.
