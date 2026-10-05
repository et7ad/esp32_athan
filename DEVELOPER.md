# Developer notes (version 3 firmware)

How the firmware is put together, for anyone changing it. Owner-facing behaviour is in `README.md`, the design
reasoning in `version3_planning.md`, and the short list of rules in `CLAUDE.md`.

## 1. Files

| Path | What |
|---|---|
| `firmware/athan.yaml` | All behaviour: entities, menus, schedule, OLED, scripts, intervals (ESPHome + inline C++ lambdas) |
| `firmware/partitions.csv` | 16 MB layout: nvs, otadata, phy, app0/app1 (0x360000 each), `prayer` (0x41, 128 KB), `audio` (0x40, 0x910000) |
| `firmware/components/athan/__init__.py` | Component schema: `media_player`, `time_id`, `data_url`, `radio_urls` (list of `text` ids). Requests the MP3 decoder, the certificate bundle, `esp_http_client` and `esp-tls` |
| `athan.h/.cpp` | `AthanComponent`, the worker task, `AudioWebHandler` (the `/audio` page) |
| `audio_slots.h/.cpp` | The four stored sounds |
| `prayer_store.h/.cpp` | Yearly file parser + the stored years |
| `tz_posix.h/.cpp` | POSIX TZ parser, offset/UTC conversion, calendar helpers (no ESPHome dependency) |
| `mp3_check.h/.cpp` | MP3 validity and duration (no ESPHome dependency) |
| `firmware/tests/` | `run_tests.sh` (host unit tests), `syntax_check.sh` (type check), `idf_stubs/` (SDK stand-ins) |
| `firmware/sounds/` | `click.mp3`, `volume.mp3`: built-in tones (`media_player: files:`), added by the builder |

## 2. Boot

1. ESPHome setup by priority. `AthanComponent::setup()` (priority `AFTER_WIFI`) does the following:
   1. Maps the audio regions and verifies each header and data CRC (`AudioSlots::begin`).
   2. Reads the prayer slot headers (`PrayerStore::begin`).
   3. Starts the worker task (`athan_worker`, 10 KB internal stack, priority 2).
   4. Registers the `/audio` handler on `web_server_base`.
2. `esphome: on_boot` (priority -100):
   1. Copies `volume_level` into `fajr_volume_level` the first time (when it is -1).
   2. Calls `set_location(selected_location_key)`.
   3. Handles the two power-up button gestures: both buttons forget Wi-Fi and restart; exactly one unlocks the
      buttons.
   4. Applies the volume and draws the screen.
3. `AthanComponent::loop()` runs a 1 s tick:
   1. Fetches the catalog and the station list when due.
   2. Auto-installs the default into an empty slot (catalog entry 0; per slot at most every 30 min).
   3. Runs `prayer_tick_()` and `radio_tick_()`.
4. The yaml's 1 s interval watches `schedule_version()` and reruns `load_today` when it changes (new day, new
   data, new location).

Nothing in a setup-time trigger may draw or play (`CLAUDE.md`). Template switches use `restore_mode: DISABLED`.

## 3. Component API (called from yaml lambdas as `id(athan_core).…`)

| Method | Use |
|---|---|
| `slot_valid(s)`, `slot_label(s)` | Slot 0 athan, 1 fajr, 2 tawashih, 3 tick |
| `play_slot(s)` → bool | Plays the mapped file on the announcement pipeline. False if the slot is empty or being rewritten (the yaml then plays three click tones) |
| `slot_playing()` | True from `play_slot()` until the player leaves `ANNOUNCING` (or 5 s if it never got there) |
| `install_from_catalog(list, entry)` | Downloads → checks → writes; progress in `sound_status()` |
| `sound_busy()`, `sound_status()` | One install/upload at a time |
| `fetch_catalog()`, `catalog_ready()`, `catalog_size(l)`, `catalog_name(l, e)` | Suggested lists |
| `preview_catalog(l, e)` | Streams an entry on the media pipeline (replaces the radio), nothing stored |
| `stop_media()` | Stops the media pipeline (preview or radio) |
| `radio_play(slot, subscribed)`, `radio_stop()`, `radio_active()` (-1 or slot) | Radio |
| `station_available(slot, subscribed)`, `station_name(slot)`, `own_url(slot)`, `radio_status()` | Radio menu/web |
| `set_location(key)`, `location()` | Changing the key resets the fetch timers and bumps `schedule_version()` |
| `schedule_version()` | Changes whenever today's times may have changed |
| `today_times(h[7], m[7])` → bool | Fajr, Sunrise, Doha, Dhuhr, Asr, Maghrib, Isha for today (local), stand-in included |
| `times_standin()` | Today's times come from last year |
| `refresh_prayer_times()`, `prayer_status()` | Force a fresh current-year download; status line |
| `web_action`, `upload_begin/data/end`, `render_audio_page` | Used by `AudioWebHandler` only |

## 4. Threading

- **Main loop:** the yaml, `loop()`, the media player calls, entity publishing, the stored-sound map.
- **Worker task:** a FIFO of `Job`s (`CATALOG`, `STATIONS`, `STATION_PLAY`, `INSTALL_URL`, `COMMIT_BUFFER`,
  `PRAYER_YEAR`). It does HTTPS (`http_get_`: crt bundle, manual redirect loop up to 5 hops, 20 s timeout, size
  limit before and during the read, PSRAM buffer), JSON parsing and flash erase/write. Results come back with
  `defer()`.
- **httpd task:** `AudioWebHandler` renders the page from copies taken under `mutex_` and queues
  install/preview/stop in `web_actions_`. Uploads are appended into a PSRAM buffer under `mutex_`, and
  `upload_end()` queues `COMMIT_BUFFER`.
- **Slot rewrite handshake:** after the file passes every check, the worker `defer`s a request to the main loop
  to `invalidate()` the slot. The main loop refuses if that slot is playing. The worker waits up to 10 s on a
  semaphore owned by a `shared_ptr<Handshake>`, so a late answer can't touch a dead stack frame. Then it writes,
  and `defer`s `reload()`.

`mutex_` guards the lists, the status strings, `web_actions_` and the upload buffer. `catalog_ready_`,
`stations_ready_`, `sound_busy_` and the pending flags are atomics. Retry deadlines are `uint64_t` from
`millis_64()`.

## 5. Stored sounds

**Regions** (inside the `audio` partition):

| Slot | Offset | Size | File limit | Duration limit |
|---|---|---|---|---|
| 0 athan | 0x000000 | 0x2E0000 | 3,000,000 B | 5 min |
| 1 fajr | 0x2E0000 | 0x2E0000 | 3,000,000 B | 5 min |
| 2 tawashih | 0x5C0000 | 0x2E0000 | 3,000,000 B | 5 min |
| 3 tick | 0x8A0000 | 0x070000 | 400,000 B | 1 min |

**Header** (first 4 KB sector of the region): `magic "ATH1"`, `version 1`, `length`, `data_crc`, `duration_ms`,
`label[64]`, `header_crc` (CRC-32 of the fields before it). The MP3 follows at +4 KB. `begin()` and `reload()`
check both CRCs, then memory-map the region (`esp_partition_mmap`) and wrap it in an `audio::AudioFile`
(`audio::AudioFileType::MP3`), which `play_slot()` hands to `SpeakerMediaPlayer::play_file(file, announcement=true)`.

**Install** (`INSTALL_URL`):
1. Download into PSRAM. A `Content-Length` over the limit is refused before reading, and the read aborts as
   soon as it passes the limit.
2. `mp3_scan()` needs three chained Layer III frames to sync (an ID3v2 tag is skipped). The duration is the sum
   of the frame durations.
3. Size and duration are checked against the slot's limits.
4. Handshake (section 4), erase, 4 KB bounce writes with progress, header last.

Any failure frees the buffer and leaves the slot as it was. A power cut during the write leaves no valid header,
so the slot is empty and the default comes back automatically.

**Upload** (`POST /audio/upload?slot=N`, multipart): `upload_begin` allocates limit + 1 bytes of PSRAM and claims
`sound_busy_`. `upload_data` appends, marking `overflow` past the limit. `upload_end` either reports "too big"
or queues `COMMIT_BUFFER`, which runs steps 2–4 above.

## 6. Lists and radio

- **Catalog** `docs/audio/catalog.json` (≤ 64 KB): lists `athan`, `fajr`, `tawashih`, `tick`, each ≤ 10
  `{name, url}`. A relative URL resolves against `<data_url>/audio/`. It is fetched at boot, then every 12 h
  (retry every 1 min on failure), and kept in RAM only.
- **Stations** `docs/radio/stations.json` (≤ 16 KB): 10 `{name, url}`. Fetched every 6 h (retry every 1 min),
  and again on every play of a subscribed slot (`STATION_PLAY`). If that fetch fails, the cached link is used and
  the status says "(cached link)".
- **`radio_play(slot, subscribed)`:** a subscribed slot queues `STATION_PLAY`, an own slot plays the slot's
  `text` entity (`radio_url_N`). Each play bumps `radio_token_`, so a stale answer from an older request is
  dropped.
- **`radio_tick_()`:** when the stream stops on its own more than 15 s after starting, it reconnects up to 3 times,
  then gives up with "station not reachable".
- **The yaml side:**
  - `make_athan` and `run_prefajr` save `radio_active()` into `radio_resume`, stop the radio, and restart that
    slot when they finish.
  - `silence_audio` clears `radio_resume`.
  - `play_tick` ducks `media_mixer_input` by 20 dB.

## 7. Prayer times

**Store** (`prayer` partition, 8 × 16 KB slots):
- Each slot is a header sector (`magic "PTY1"`, `version`, `seq`, `key[32]`, `year`, `days`, `tz[64]`,
  `table_crc`, `header_crc`) plus the table at +4 KB: `uint16` minutes per value, 12 per day.
- `write()` picks a free slot or the least useful one, never one in the keep list (key's year−1, year,
  year+1), and writes the header last. If duplicates exist, the highest `seq` wins.

**Device checks** (`parse_year_file`): `prayertimes_specs.md` "What the clock checks".

**`prayer_tick_()`** (main loop, every second, after the clock is valid):
1. Loads the current and previous year's tables for the location (`load_tables_`), and applies the time zone
   from the stored year (`apply_tz_` → `time::ParsedTimezone` → `time::set_global_tz`).
2. Bumps `schedule_version_` at a new day of year.
3. Publishes the status line.
4. Queues at most one `PRAYER_YEAR` job:

| Purpose | When | Retry after |
|---|---|---|
| CURRENT | Current year not stored, or Refresh pressed | 404: 30 min if nothing to stand in, else 6 h · invalid: 30 min · network: 2 min |
| PREVIOUS | Current year 404 and no previous year stored (new device in January) | network: 5 min · otherwise 6 h |
| NEXT | December and next year not stored | 404/invalid: 24 h · network: 1 h |

**`today_times()`:** the stored current year if present. Otherwise the stand-in from the previous year: the same
month/day (29 Feb → 28 Feb), each value converted through UTC with that date's offsets in both years, clamped
to 00:00–23:59.

**The yaml side:**
- `load_today` copies the 7 times into `prayer_hours`/`prayer_minutes` (not persisted) and runs
  `compute_coming_prayer`.
- The 1 s interval fires the athan when the next prayer's minute is reached. After Isha it waits for midnight
  before matching tomorrow's Fajr. Then `jump_to_next_prayer` runs.
- Pre-Fajr Tawashih fires once at Fajr − `prefajr_offset_min`.

## 8. The yaml

**Persisted globals (NVS, keyed by id):** `volume_level`, `fajr_volume_level` (-1 = copy from volume at boot),
`athan_enabled[7]` (indices 0, 3, 4, 5, 6 used), `buttons_locked`, `clock_12h`, `htick_enabled`,
`htick_start_hour`, `htick_end_hour`, `prefajr_enabled`, `selected_location_key` (string, max 31),
`radio_subscribed[10]`, `radio_slot`. The ten `radio_url_N` text entities persist themselves
(`restore_value: true`). The relay uses `RESTORE_DEFAULT_OFF`.

**Scripts:**

| Script | Does |
|---|---|
| `apply_volume`, `apply_fajr_volume` | Set the player volume |
| `play_tone_click`, `play_tone_volume` | Play the built-in tones, not over an athan |
| `fajr_volume_feedback` | Web slider: play a tone at the Fajr level, then restore |
| `amp_idle_off` | Amp off 5 s after the player goes idle |
| `silence_audio` | Stop everything, cancel the radio resume |
| `radio_start` | Play `radio_slot` |
| `make_athan` | Regular or Fajr slot, Fajr volume at Fajr, LED on, waits for the end (≤ 7 min), resumes the radio |
| `run_prefajr` | Relay on, tawashih at the Fajr volume, resume the radio, relay off after `prefajr_relay_min` |
| `play_tick` | Tick over the ducked radio |
| `check_update` | `update.check`, then sets `update_check_state` for the menu |
| `load_today`, `compute_coming_prayer`, `jump_to_next_prayer` | Schedule |
| `sync_web_state` (1 s interval) | Publish entities only on change |
| `update_display` | The whole OLED |

**Intervals:** 1 s schedule, 1 s `sync_web_state`, 10 s OLED watchdog (ESPHome setup for a never-initialised
display; one in-place `setup()` per boot for a display that came back).

**Menu and `ui_mode`:** CLAUDE.md "Menu state machine". The main-menu indices appear in three places, so move
them together.

**Wi-Fi:**
- No network is compiled in: `wifi: ap:` (AthanFallbackHotspot / athan404, `ap_timeout: 3min`) +
  `captive_portal` + `esp32_improv` (authorizer = Select, 1 min, `wifi_timeout: 15s`, LED as status).
- BLE is disabled on Wi-Fi connect and enabled on disconnect. `esp32_ble: use_psram: true`.
- The setup screen reads `id(improv_ble).get_improv_state()`. `on_state` redraws it.

## 9. Web

- ESPHome's page (`web_server` v3) with sorting groups: Now, Athan, Athan On/Off per Prayer, Sounds, Radio,
  Radio Stations, Location and Prayer Times, System.
- `/audio` (custom handler): `GET /audio` renders the page (refreshes itself every 3 s while busy).
  `POST /audio/install|preview|stop?list=&entry=` queue an action and redirect back. `POST /audio/upload?slot=`
  is a multipart upload. Plain HTML, no JavaScript.
- Firmware updates: `update: platform: http_request` with the Releases manifest (every 6 h, the Update menu
  item, the Check For Update button). `ota:` has `esphome` (encrypted, same key as the API), `http_request` and
  `web_server` (needed for multipart uploads to reach custom handlers).

## 10. Checks you can run without hardware

`firmware/tests/run_tests.sh`:
- Calendar helpers.
- The POSIX TZ parser against Python `zoneinfo`, every 30 min over 2025–2028 for PST8PDT, Cairo, Berlin and
  Riyadh (280,512 vectors).
- With MP3 paths as arguments, `mp3_scan` against `ffprobe`.

`firmware/tests/syntax_check.sh`:
- Generates the C++ with `esphome compile --only-generate` on a temp copy (placeholder tones if
  `firmware/sounds/` is empty).
- Type-checks the component sources (`-Wall -Wextra`) and `main.cpp` (all lambdas) with `clang++
  -fsyntax-only` against the real ESPHome headers. `idf_stubs/` stands in for ESP-IDF and library headers.

## 11. To verify on the prototype

The full acceptance list is `version3_planning.md` section 13. These points in this implementation rest on
assumptions that only hardware can confirm:

- Firmware size against the 0x360000 app slots, and internal heap with BLE + hotspot + an athan.
- `play_file()` of a memory-mapped `AudioFile` from the `audio` partition (the mmap stays valid while playing;
  `invalidate()` refuses a playing slot).
- How the player reports state while an announcement plays over the radio. `slot_playing()` relies on
  `ANNOUNCING`.
- `esp_http_client` redirects for `releases/latest/download` (handled by ESPHome's `update` component) and for
  raw.githubusercontent.com (no redirect normally).
- Whether ESPHome opens the hotspot immediately on a device with no saved network.
- Stream reconnect behaviour after a router restart.
- The MAX98357A gain jumper and the 560 kΩ SD_MODE resistor (`max98357a_amplifier.md`).
