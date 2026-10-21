# Maintaining the data

How to keep what the clocks download up to date: prayer times, the sound library, the radio stations, and firmware
releases. This is for whoever maintains the project.

1. [How the clocks get their data](#1-how-the-clocks-get-their-data)
2. [Prayer times](#2-prayer-times)
3. [Sounds](#3-sounds)
4. [Radio stations](#4-radio-stations)
5. [Firmware releases](#5-firmware-releases)
6. [The website](#6-the-website)

## 1. How the clocks get their data

Everything the clocks download lives under `docs/` on the `main` branch, and they read it over HTTPS from
`https://raw.githubusercontent.com/et7ad/esp32_athan/main/docs/...`. A change there reaches every clock without a
firmware update. `firmware/tests/run_tests.sh` checks everything under `docs/` (the lists, the stations, the
mosques and every yearly file) before you push. The same folder is also the project's website (section 6); the clocks
read only their own files in it.

## 2. Prayer times

There is one file per mosque per year: `docs/athantimes/<key>/<year>.json`. The format is in
[prayertimes_specs.md](../prayertimes_specs.md): a list of 12 `HH:MM` times for each day, in 24-hour time, at most
100 KB.

The scripts in `scripts/` each write exactly that one file. They refuse a year the clock would refuse (adhan times
out of order, a wrong number of days, a key or time zone the clock does not accept), and they warn about days that
look wrong: an iqama before its adhan, a time more than 3 minutes away from the same date last year, or more than 5
minutes away from the day before. Both comparisons go through UTC, so daylight saving changes don't count.

| Your source | Script |
|---|---|
| A table (a spreadsheet exported as a JSON list of rows) | `python3 scripts/source_to_yearly.py --key davis davis_2027.json` |
| IslamicFinder's yearly page (saved as HTML) | `python3 scripts/parse_Islamicfinder/parse_islamicfinder_yearly.py --key masjid15 Visalia2027.html` |
| The MCA Santa Clara PDF | `python3 scripts/santaclara_prayer_times_parser.py --key sclaramca 2027_MCA_Prayer_Time.pdf` |
| A year that exists only as version 2 daily files | `python3 scripts/make_yearly_json.py --key davis --year 2027` (reads `../esp_athan`) |

Add `--dry-run` to check without writing. The PDF and HTML parsers need `pip install pdfplumber` and
`pip install beautifulsoup4`.

The website's **Prayer times** page draws every published year of every mosque on a chart, straight from these
files, so a typo stands out at a glance there (in a local preview before you push: section 6). Offline,
`python3 scripts/plot_prayer_times.py docs/athantimes` draws the same charts as Plotly pages next to each file, with
`docs/athantimes/charts.html` linking them (it needs `pip install plotly`).

**Publish each new year as soon as the mosque publishes its timetable,** whether that is in November or in
January. Clocks pick it up within a day in December, or within 6 hours from 1 January. Until then they estimate the
times from last year's.

A clock downloads a year only once. If you correct a year that clocks already have, each clock loads the corrected
file when its owner presses **Refresh Prayer Times** on its web page ([using.md](using.md) section 7), so let the
owners at that mosque know.

Version 2 clocks read daily files from the version 2 repository, [et7ad/esp_athan](https://github.com/et7ad/esp_athan).
Make those with that repository's own scripts and commit them there; the scripts here write only the yearly files.

### Adding a mosque

1. Add it to [docs/athantimes/mosques.json](../docs/athantimes/mosques.json):
   - its key: the folder name, lowercase letters, digits and `_`, at most 31. Only the files and the clocks use it;
     owners never see it;
   - its name, which the clocks show in their menu and on their web page: plain ASCII, at most 40 characters,
     different from every other mosque's;
   - where it is, `lat` and `lon` in decimal degrees as a map shows them (in Google Maps, right-click the building:
     the first line is the two numbers). Colour-screen clocks show them under the name;
   - its POSIX time zone (for example `PST8PDT,M3.2.0,M11.1.0`, not `America/Los_Angeles`), and how many minutes
     after sunrise Doha is.
2. Make its yearly file or files with one of the scripts above.
3. Add it to the table in [using.md](using.md) section 7: its name, its map link and its years.
4. If version 2 clocks there need it too, add it to the version 2 repository as well: its own Location list, daily
   files and time zone file.

No firmware update is needed. Clocks read the list of mosques from mosques.json when they start, every 6 hours, and
when an owner opens the menu's Location row if their copy is more than 2 hours old (a restart loads it at once); they
keep the list they have when they can't. They number the mosques in the file's order ("1. Islamic Center of Davis"),
so switching one off renumbers the ones after it, and store the key of the one an owner picks.

The clocks' web page holds about 150 mosques (ESPHome sends a list in one message of at most 5 KB); the clock's menu
has no limit. `firmware/tests/run_tests.sh` says when the list gets too long for the web page.

To stop offering a mosque without deleting it, add `"active": false` to its entry. It leaves the clocks' list, and
clocks already set to it keep it, shown by its name, and keep its stored years. Other clocks just drop it from
their list at their next reload.

## 3. Sounds

`python3 scripts/prepare_audio.py` builds the whole library from the version 2 recordings
(`../esp_athan/SDCard_files`: A for athan, F for Fajr, D for tawashih, B for tick). Every file comes out the same
way: MP3, mono, 44.1 kHz (the clock's own rate), 64 kbps, with no tags, cover art or info frame. Each one gets a
single gain so its loudest moment sits near -2 dBFS, which avoids clipping and keeps the recordings at a similar
loudness. Each file is checked to be exactly as long as its source; nothing is sped up or cut, and a recording over
the length limit is refused.

Add `--check` to see what it would do without writing anything. A file that fails a check keeps the file that was
there. The source `A05.mp3` counts as entry 5; two sources for one entry are refused.

The library itself:

- The files are in `docs/audio/athan/`, `fajr/`, `tawashih/` and `tick/`, named `01.mp3`, `02.mp3`, and so on.
- Their names and links are in [docs/audio/catalog.json](../docs/audio/catalog.json). A list can have any number of
  entries (the whole file can be up to 64 KB, over 1,000 entries). The clock and its web page number them in the
  file's order ("1. Athan 1", "2. Abdul Basit"), so add new ones at the end and the numbers owners know stay the
  same.
- Entry 1 of each list is the default that new clocks download.
- A `url` can be relative to `docs/audio/` (`athan/01.mp3`) or a full https link.
- Names are plain ASCII, at most 12 characters, so they fit the small screen.
- Every file has to pass the clock's checks ([using.md](using.md) section 5). The `ffmpeg` line there makes a big
  file smaller; use `48k` for ticks.
- Clocks read the list again twice a day, and when an owner opens one of the menu's sound rows if their copy is more
  than 2 hours old (a restart loads it at once).

Entries whose reciter is not known yet keep a placeholder name ("Athan 1", "Tick 1"). Rename them in the list when
it is.

## 4. Radio stations

[docs/radio/stations.json](../docs/radio/stations.json) has exactly 10 entries: slot *k* of a clock that follows
the project's list plays entry *k*. Change a link there and every such clock uses it the next time that slot plays.
An empty `url` leaves that slot empty. Names follow the same 12-character rule as the sounds.

Every link must meet the rules in [using.md](using.md) section 6 ("Your own station"). Check each one with:

```bash
curl -sI -X GET --max-time 5 <url>
```

The clock refuses an `https://` link that forwards to an `http://` address. If a station only offers that, use an
HTTPS source that answers `200` directly, or list the `http://` form. Egypt's Quran Radio is an example: its
official radiojar link (`stream.radiojar.com/8s5u5tpdtwzuv`) forwards to HTTP, so slot 1 uses xecod.com's HTTPS
relay; the radiojar link, written with `http://`, is the fallback if the relay goes away.

## 5. Firmware releases

1. Raise `project_version` in `firmware/athan.yaml` (for example to `3.0.1`) and run `./build_firmware.sh`. It
   writes `athan-v3.ota.bin` and a matching `manifest.json` into `firmware/binaries/`.
2. If you build another way (in the ESPHome dashboard: ⋮, Download, OTA format), make the two files from the OTA
   image yourself:

   ```bash
   python3 scripts/make_release_manifest.py path/to/firmware.ota.bin --version 3.0.1 --summary "What changed"
   ```

   It refuses a factory image, an image built for another display than `--display` (add `--display tft` for TFT
   builds), and a `--version` the image was not built with. Nothing is written then.
3. Create a GitHub Release with the tag `v3.0.1`, attach the two files (`athan-v3.ota.bin` and `manifest.json`),
   and mark it as the latest release. Clocks see it at their next start or their daily check (3:00), and show the
   update mark until their owners install it; no clock installs an update by itself.
4. For colour-screen clocks, also run `./build_firmware.sh --display tft` and attach its two files,
   `athan-v3-tft.ota.bin` and `manifest-tft.json`, to the same release. TFT clocks read only `manifest-tft.json`,
   so a release without those files leaves them on their current version.

A change to `firmware/partitions.csv` cannot be delivered as an update: every clock then needs a full flash over
USB ([building.md](building.md) section 4).

Version 2 clocks read only their own repository's update file, so they never see these releases.

## 6. The website

The website, [et7ad.github.io/esp32_athan](https://et7ad.github.io/esp32_athan/), is GitHub Pages serving `docs/` on
`main` as it is (`docs/.nojekyll` switches off GitHub's Jekyll processing). In the repository's settings, Pages takes
the branch `main` and the folder `/docs`. A push to `main` updates the site within a minute or two.

- **The pages** are `docs/*.html`, plain HTML with their scripts and styles in `docs/assets/`, and no build step.
  They read the clocks' own files (the mosques, the yearly times, the sound list and the stations), so a mosque, a year
  or a sound added there appears on the site by itself.
- **The guides** (`read.html`) are the repository's Markdown files, loaded from GitHub as they are: a change to a
  guide reaches the site with the push.
- **Pictures** from outside `docs/` (`images/`, `hardware/…/images/`) come from GitHub too; the site's own screenshots
  are in `docs/assets/img/`.
- **The clock in the browser** (`docs/assets/tft.js`) is a copy of the colour screen's drawing, its menu and its keys.
  When the firmware's screen, menu rows or keys change, change it there too.
- **The parts and the checklist** on the build page are `docs/assets/parts.js`, from HARDWARE.md section 7 and
  building.md sections 1, 2 and 7: keep them in step.
- **The 3D models** (`docs/assets/models/`) are made from the case's assembly files. After
  `hardware/enclosure/enclosure.py --assembly`, run
  `uv run --python 3.12 --with cadquery python scripts/make_site_models.py` (it needs Node.js for gltfpack).

To preview before pushing, serve the repository's root and open the `docs/` folder:

```bash
python3 -m http.server 8000
# then open http://localhost:8000/docs/
```

The preview reads pictures and guides from your working copy, so changes show before they are pushed.
