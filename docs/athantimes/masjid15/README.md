# masjid15: Visalia, California

The prayer times under this key (`docs/athantimes/masjid15/<year>.json`) are for **Visalia, California, USA**
(Pacific time, `PST8PDT,M3.2.0,M11.1.0`). The key keeps its V2 name `masjid15`: V2 clocks store slot positions and
already point at it, and version 3 clocks store the key itself.

## Where the data comes from

[IslamicFinder](https://www.islamicfinder.org/)'s yearly prayer-times page for Visalia, CA.

1. **Save the page's HTML** from the browser; the steps are in
   `scripts/parse_Islamicfinder/parse_islamicfinder_yearly_get_html.jpg`. Save it as, for example,
   `scripts/parse_Islamicfinder/preparing/Visalia2027.html`.
2. **Convert it** into the one yearly file:

   ```bash
   python3 scripts/parse_Islamicfinder/parse_islamicfinder_yearly.py --key masjid15 --dry-run   # check
   python3 scripts/parse_Islamicfinder/parse_islamicfinder_yearly.py --key masjid15             # write
   ```

   Without a path it reads every `*.html` in `preparing/`. It writes `docs/athantimes/masjid15/<year>.json`
   (format: `prayertimes_specs.md`).

## Iqama times are estimates

IslamicFinder gives only adhan times and sunrise. The script derives the rest from
`docs/athantimes/mosques.json`:

- `doha` = sunrise + `doha_after_sunrise` (15 min for masjid15)
- each iqama = adhan + `iqama_after_default` (Fajr 30, Dhuhr 10, Asr 10, Maghrib 10, Isha 20)

These are not a specific mosque's congregation times. If a local mosque's schedule becomes available, give
masjid15 its own `"iqama_after"` in `mosques.json` or switch to that mosque's timetable.

V2 clocks read daily files from the V2 repository ([et7ad/esp_athan](https://github.com/et7ad/esp_athan)); produce
those with that repository's own scripts.
