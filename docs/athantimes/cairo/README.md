# cairo: Cairo, Egypt

The prayer times under this key (`docs/athantimes/cairo/<year>.json`) are for **Cairo, Egypt**
(Egyptian time with daylight saving, `EET-2EEST,M4.5.5/0,M10.5.4/24`).

## Where the data comes from

Egypt's official prayer times are computed by the **Egyptian General Authority of Survey** (Fajr angle 19.5°,
Isha angle 17.5°) and published, day by day, by **Dar al-Ifta**: <https://www.dar-alifta.org/ar/prayer>. That page
serves only the current month — no yearly table, no download, no public API (checked 2026-11-05) — so the data
comes from the [Aladhan API](https://aladhan.com/prayer-times-api), whose calculation method 5 is named exactly
"Egyptian General Authority of Survey": the same origin, at Cairo's coordinates (30.044420, 31.235712).

**Accuracy check (2026-11-05):** every day of dar-alifta.org's November 2026 month tables for Cairo, Alexandria
and Damietta (30 days × 6 times × 3 cities) was compared with the API's output: every single time agreed within
**±1 minute**, most exactly (boundary rounding, no systematic bias).

The fetch lives in the V2 repository ([et7ad/esp_athan](https://github.com/et7ad/esp_athan)), which needs the same
cities as daily files; this repository's yearly files are converted from those:

```bash
# in ../esp_athan — fetch the daily files (details: its docs/athantimes/cairo/README.md)
python3 scripts/aladhan/fetch_aladhan_daily.py --key cairo --year 2026 --year 2027 --year 2028
# in this repository — one yearly file per year, converted from the daily files
python3 scripts/make_yearly_json.py --key cairo --year 2026 --year 2027 --year 2028
```

## Iqama times are estimates

The source gives adhan times and sunrise only. Doha and the iqama fields are derived, with the same offsets as
masjid15: Doha = sunrise + 15 min; iqama = adhan + (Fajr 30, Dhuhr 10, Asr 10, Maghrib 10, Isha 20) min. These
are **not a specific mosque's congregation times**.

## Daylight saving for future years

Egypt's current law switches to EEST (UTC+3) from the last Friday of April at 00:00 until the last Thursday of
October at 24:00 — exactly what the `tz` in `mosques.json` (copied into every yearly file) encodes, and the V2
fetch script refuses API data that disagrees with that rule. 2027 and 2028 are computed under the current law:
**if Egypt changes its DST policy, regenerate those years and change the `tz` at the same time** (here and in the
V2 repository's `docs/timezones/`).
