# Developer notes (version 3 firmware)

How the firmware is put together, for anyone changing it. Owner-facing behaviour is in
[guides/using.md](guides/using.md), the board's design in [HARDWARE.md](HARDWARE.md), the prototype's acceptance
checklist in [guides/building.md](guides/building.md) section 7, why this chip in `microcontroller_demand.md`, and
the short list of rules the code depends on in [firmware_rules.md](firmware_rules.md).

## 1. Files

| Path | What |
|---|---|
| `firmware/athan.yaml` | All behaviour: entities, menus, schedule, scripts, intervals (ESPHome + inline C++ lambdas). `display_type` (oled or tft) picks the display file |
| `firmware/displays/oled.yaml` | The OLED build's display part: I2C, the SSD1306/SH1106, fonts, `update_display` (clock screen, pop-ups, Wi-Fi setup), the hot-plug watchdog |
| `firmware/displays/tft.yaml` | The TFT build's display part: SPI, the ST7789 (`mipi_spi`), the PWM backlight (`apply_screen`), Roboto and Material Symbols fonts (bpp 4), the Screen Colour setting (global, web select, its menu row in `menu_display_rows`), and every screen in the display's `lambda:` (the ring clock, pop-ups, Wi-Fi setup) |
| `firmware/partitions.csv` | 16 MB layout: nvs, otadata, app0/app1 (0x360000 each), `prayer` (0x41, 128 KB), `audio` (0x40, 0x910000) |
| `firmware/components/athan/__init__.py` | Component schema: `media_player`, `media_speaker` and `announcement_speaker` (the two resamplers, for clean starts and format checks), `time_id`, `data_url`, `radio_urls` (list of `text` ids), `display` (oled or tft: defines `ATHAN_DISPLAY_OLED` / `ATHAN_DISPLAY_TFT`). Requests the MP3 decoder, the certificate bundle, `esp_http_client` and `esp-tls` |
| `athan.h/.cpp` | `AthanComponent`, the worker task, `AudioWebHandler` (the `/audio` page) |
| `audio_slots.h/.cpp` | The four stored sounds |
| `prayer_store.h/.cpp` | Yearly file parser + the stored years |
| `day_window.h` | The hourly tick's and the screen's night windows: an end is an hour or a prayer at today's times, stored in the same int settings (`WINDOW_PRAYER` + the prayer's index). `WindowKind`: the night runs from Maghrib or Isha until Fajr or Dhuhr, the tick's day window from Fajr or Dhuhr until Maghrib or Isha; the menu rows and web selects share its choice order. `window_sanitize()` (on_boot) turns an end a window no longer offers into the first hour at or after that prayer (no ESPHome dependency, tested in `test_helpers.cpp`) |
| `tz_posix.h/.cpp` | POSIX TZ parser, offset/UTC conversion, calendar helpers (no ESPHome dependency) |
| `mp3_check.h/.cpp` | MP3 validity and duration (no ESPHome dependency) |
| `menu.h/.cpp`, `menu_frame.h` | The menu's navigation (host-tested), and the `Frame` both drawings read (`frame_of()`, the redraw fingerprint) |
| `menu_view.h/.cpp`, `menu_view_tft.cpp` | `draw_menu()` for the OLED (`menu_view.cpp`) or the TFT (`menu_view_tft.cpp`): one of them is compiled (`ATHAN_DISPLAY_TFT`) |
| `tft_theme.h`, `tft_draw.h/.cpp` | The TFT's colours (neutrals, the five Screen Colour palettes) and Material Symbols code points (`sym::`); `ring()` (the clock's ring: filled part and track in one pass, about 9 ms of a 50 ms frame); shapes with smoothed edges (rounded boxes, circles, ring arcs) and `icon()` |
| `firmware/tests/` | `run_tests.sh` (host unit tests), `syntax_check.sh` (type check), `idf_stubs/` (SDK stand-ins) |
| `firmware/sounds/` | `click.wav`, `volume.wav`: built-in tones (`media_player: files:`), mono 16-bit 44.1 kHz WAV (no decoding, no MP3 encoder padding ahead of the sound) |

## 2. Boot

1. ESPHome setup by priority. `AthanComponent::setup()` (priority `AFTER_WIFI`) does the following:
   1. Maps the audio regions and verifies each header and data CRC (`AudioSlots::begin`).
   2. Reads the prayer slot headers (`PrayerStore::begin`).
   3. Starts the worker task (`athan_worker`, 10 KB internal stack, priority 1: the same as ESPHome's main loop and
      the audio pipeline's tasks, which a TLS handshake at a higher priority held off for hundreds of ms).
   4. Registers the `/audio` handler on `web_server_base`.
2. `esphome: on_boot` (priority -100):
   1. Copies `volume_level` into `fajr_volume_level` the first time (when it is -1), and switches the relay off when
      Pre-Fajr had it on at the restart (`relay_by_prefajr`).
   2. `window_sanitize()` on the four window ends, then `set_location(selected_location_key)`, and the component's
      two callbacks (`set_preview_volume_callback`, `set_hard_stop_callback`).
   3. Adds the menu's rows (`menu_setup`).
   4. Handles the two power-up gestures of the 5-way switch: Select held forgets Wi-Fi and restarts; any
      direction held unlocks the buttons.
   5. Applies the volume and draws the screen.
3. `AthanComponent::loop()` runs a 1 s tick:
   1. Fetches the catalog and the station list when due.
   2. Downloads the default into an empty slot (catalog entry 0; per slot at most every 30 min). While the location
      has no prayer times at all, this waits until their download has run once (`prayer_first_()`, at most 90 s after
      the network came up): a year of times is about 10 KB, a sound 3 MB plus a flash erase, on the same worker.
   3. Runs `prayer_tick_()` (its job goes to the front of the queue while no times are loaded) and `radio_tick_()`.
   `net_watch_()` restarts SNTP when the network comes up and the clock has no time: ESPHome starts SNTP once at
   boot, and each request made without a network doubles lwIP's retry wait (15 s up to 150 s).
4. The yaml's 1 s interval watches `schedule_version()` and reruns `load_today` when it changes (new day, new
   data, new location).

Nothing in a setup-time trigger may draw or play (`firmware_rules.md` 3.2). Template switches use `restore_mode: DISABLED`.

## 3. Component API (called from yaml lambdas as `id(athan_core).…`)

| Method | Use |
|---|---|
| `slot_valid(s)`, `slot_label(s)` | Slot 0 athan, 1 fajr, 2 tawashih, 3 tick |
| `play_slot(s)` → bool | Plays the mapped file on the announcement pipeline, once that pipeline has stopped (clean start). False if the slot is empty or being rewritten (the yaml then plays three click tones) |
| `play_tone(file)`, `stop_announcements()` | The only way the yaml starts or stops the announcement pipeline: a tone waits for the sound before it to stop; never over the athan, tawashih or tick |
| `set_hard_stop_callback(cb)` | The yaml's `hard_mute`, called when browsing leaves a playing preview, by `stop_preview()`, and for the `/audio` page's Stop |
| `set_wifi_bg_only(on)` (yaml `wifi_bg_only:`) | Wi-Fi 802.11b/g only (`esp_wifi_set_protocol`), set in `setup()` during ESPHome's first scan and at every `WIFI_EVENT_STA_START` |
| `set_wifi_low_latency(on)` | Wi-Fi modem sleep off (`on`) or back on. Only while Bluetooth is off: the yaml calls it after `ble.disable` has finished and before `ble.enable` (`ble_when_offline`, 15 s after Wi-Fi went) |
| `slot_playing()` | True from `play_slot()` until the player leaves `ANNOUNCING` (or 5 s if it never got there) |
| `download_from_catalog(list, entry)` | Downloads → checks → writes; progress in `sound_status()`, then "2. Abdul Basit downloaded and in use (3:12)". The selected entry is never downloaded again ("already downloaded and in use"); downloading another entry first fetches it anew |
| `selected_entry(list)` | Index of the list entry (the selected one) whose link matches the slot's stored source CRC (version 1 headers: the label), -1 for an upload or an unlisted sound. Each entry's CRC is worked out when the list loads, and the answer is kept per slot (`selected_`) until the list (`catalog_gen_`) or the stored sound changes: the menu asks once per item per draw, and a list can be long |
| `sound_name(s)` | The slot's sound as the menu and web page name it: "2. Abdul Basit" (a list entry, `numbered()`), the file's name (an upload), "" (empty). The web page's Selected Sounds |
| `sound_busy()`, `sound_status()` | One download/upload at a time |
| `fetch_catalog()`, `catalog_ready()`, `catalog_size(l)`, `catalog_name(l, e)` | Suggested lists, any number of entries each. `catalog_name` is the bare name; `athan::numbered(e, name)` (`menu.h`) gives "e+1. name" |
| `has_custom(list)` | The slot holds a sound that is not a list entry: an upload (source 0), or a download the loaded list no longer has. Shown as "Custom" in the menu and on `/audio` |
| `menu_size(l, off)`, `menu_item(l, off, i)`, `menu_index(l, off, item)` | The OLED sound list: `MENU_OFF` (tick only), `MENU_RANDOM` (tawashih, first), `MENU_CUSTOM` (while `has_custom`), then the entries. `MENU_NONE` for an index with nothing there |
| `tawashih_random()`, `set_tawashih_random(on)` | The Random tawashih (default, saved as a preference). On frees the stored tawashih (`CLEAR_SLOT` once nothing plays from it); a download or upload started after the choice turns it off |
| `play_random_tawashih()`, `tawashih_streaming()`, `stop_tawashih()` | Pre-Fajr with Random: streams a random entry (not the last one), one retry with another entry if it fails at once; false without internet or list |
| `has_stored_times()` | The location has stored prayer times: the timetable shows the last day's times and the next prayer (saved globals) struck through until the clock is set |
| `request_preview(l, item, ms)` | How the menu and `/audio` start previews: stops the current preview at once (one STOP), starts the new one (`preview_catalog` or `preview_stored`) after `ms` without another request. Menu Left/Right use 500 ms, reaching a sound row 800 ms, `/audio` 300 ms. Keeps fast browsing to one stream and a few media commands (`firmware_rules.md` 3.1, "Never send media player commands in bursts") |
| `preview_stored(list)` | Plays the stored sound from flash as a preview (the Custom item; `/audio`'s Custom line, `entry=-1`) |
| `preview_catalog(l, e)` | Nothing stored. The selected entry plays from flash (`play_slot`, marked as a preview); any other streams on the media pipeline. Either replaces the radio. Refused while the athan, tawashih or tick plays |
| `stop_media()` | Stops the media pipeline (preview or radio) and a preview playing from flash, never the athan or the tick. Drops a stream still waiting to start |
| `stop_preview()` | Stops a preview (and one still waiting for its pause) silently (`hard_stop_cb`); nothing else. The radio plays on unless a preview had replaced it. The menu calls it when it leaves a sound row |
| `menu()` | The OLED menu (`Menu`, `menu.h`): `open()`, `close()`, `key(k)`, `is_open()`, `add_row(row)`, `set_on_close(cb)`. Main loop only |
| `radio_play(slot, subscribed)`, `radio_stop()`, `radio_active()` (-1 or slot) | Radio |
| `station_available(slot, subscribed)`, `station_name(slot)`, `own_url(slot)`, `radio_status()` | Radio menu/web |
| `set_location(key)`, `location()` | Changing the key resets the fetch timers and bumps `schedule_version()` |
| `schedule_version()` | Changes whenever today's times may have changed |
| `today_times(h[7], m[7])` → bool | Fajr, Sunrise, Doha, Dhuhr, Asr, Maghrib, Isha for today (local), stand-in included |
| `times_standin()` | Today's times come from last year |
| `refresh_prayer_times()`, `prayer_status()` | Force a fresh current-year download; status line |
| `web_action`, `upload_begin/data/end`, `render_audio_page` | Used by `AudioWebHandler` only |
| `wifi_drop_count()`, `wifi_drops_text()` | Wi-Fi connections lost since start, and the last three with time, ESP-IDF reason and RSSI (an `esp_event` handler on `WIFI_EVENT_STA_DISCONNECTED` keeps the reason). The web page's Wi-Fi Drops |

Also from lambdas: `athan::draw_menu(display, menu, fonts)` (`menu_view.h`; `MenuFonts` differs per display) draws the
open menu.

## 4. Threading

- **Main loop:** the yaml, `loop()`, the media player calls, entity publishing, the stored-sound map.
- **Worker task:** a FIFO of `Job`s (`CATALOG`, `STATIONS`, `STATION_PLAY`, `DOWNLOAD_URL`, `COMMIT_BUFFER`,
  `PRAYER_YEAR`, `CHECK_STREAM`, `CLEAR_SLOT`). It does HTTPS (`http_get_`: crt bundle, manual redirect loop up to 5 hops, 20 s timeout, size
  limit before and during the read, PSRAM buffer), JSON parsing and flash erase/write. Results come back with
  `defer()`.
- **httpd task:** `AudioWebHandler` renders the page (a `PsramString`, about 14 KB) from copies taken under one
  `mutex_` lock: the catalog and `slot_view_`, a snapshot of each slot (name, length, selected entry, Custom) that
  only the main loop refreshes (`refresh_slot_view_()`: boot, catalog load, before and after a rewrite). It queues
  download/preview/stop in `web_actions_` (`loop()` looks only when `web_actions_pending_` is set). Uploads are
  appended into a PSRAM buffer under `mutex_`, and `upload_end()` queues `COMMIT_BUFFER`.
- **Slot rewrite handshake:** after the file passes every check, the worker `defer`s a request to the main loop
  to `invalidate()` the slot. The main loop refuses if that slot is playing. The worker waits up to 10 s on a
  semaphore owned by a `shared_ptr<Handshake>`, so a late answer can't touch a dead stack frame. Then it writes,
  and `defer`s `reload()`.

`mutex_` guards the lists, the status strings, `web_actions_` and the upload buffer. `catalog_ready_`,
`stations_ready_`, `sound_busy_` and the pending flags are atomics. Retry deadlines are `uint64_t` from
`millis_64()`.

## 5. Stored sounds

**Regions** (inside the `audio` partition; fixed raw regions rather than a filesystem such as LittleFS, so a sound
plays straight from flash and needs no file-reading media source):

| Slot | Offset | Size | File limit | Duration limit |
|---|---|---|---|---|
| 0 athan | 0x000000 | 0x2E0000 | 3,000,000 B | 5 min |
| 1 fajr | 0x2E0000 | 0x2E0000 | 3,000,000 B | 5 min |
| 2 tawashih | 0x5C0000 | 0x2E0000 | 3,000,000 B | 5 min |
| 3 tick | 0x8A0000 | 0x070000 | 400,000 B | 1 min |

**Header** (first 4 KB sector of the region): `magic "ATH1"`, `version 2`, `length`, `data_crc`, `duration_ms`,
`label[64]`, `source_crc` (CRC-32 of the download link, never 0; 0 for an upload), `header_crc` (CRC-32 of the
fields before it). A version 1 header (no source) is still read: its `header_crc` sits where `source_crc` is now, and such a sound is matched to the list by its
label. The MP3 follows at +4 KB. `begin()` and `reload()`
check both CRCs, then memory-map the region (`esp_partition_mmap`) and wrap it in an `audio::AudioFile`
(`audio::AudioFileType::MP3`), which `play_slot()` hands to `SpeakerMediaPlayer::play_file(file, announcement=true)`.
They also run `mp3_scan()` over the file: the `AudioFile` starts at its first chained frame (a tag or junk before
it could give ESPHome's decoder a false header, whose sample rate it would keep), and the real sample rate and
channel count are kept for the format check after each start (boot log: `… Hz, … ch`).

**Clean starts and format checks** (`pump_starts_()`, `check_formats_()`, every loop):
- ESPHome's `AudioPipeline` passes a stream's format to its speaker once, when the speaker starts; a new URL or
  file on a pipeline that still runs reuses it, and the resampler keeps converting from the previous format.
  `start_stream_()` and `start_announcement_()` therefore send one STOP if needed and start only when the player
  is no longer `PLAYING` / `ANNOUNCING` and the resampler `is_stopped()` (after 4 s / 3 s they start anyway, with
  a warning, and the format check catches a stale chain).
- Stored sounds: about 300 ms after the start, the announcement resampler's `get_audio_stream_info()` must match
  the slot's scanned rate and channels; one restart otherwise.
- Streams: the fix is in ESPHome's own pipeline (`components/speaker`, ATHAN PATCH 1, README there). microMP3's
  probe locks onto the first four bytes that look like a header, a live stream is joined mid-frame, and at the
  first real frame the decoder reports `MP3_STREAM_INFO_CHANGED`; stations also change format between recordings.
  Upstream gives the speaker the format only once, so both played at the wrong speed. The patched pipeline gives a
  stopped speaker the new format whenever the decoder's changes (and stops a still-running speaker before the first
  one, which covers the player's own restarts too).
- Streams, backstop (MP3 only): `CHECK_STREAM` reads the first 12 KB with `http_get_(…, truncate)` and
  `mp3_scan()`s them (the first position where frames chain). It runs 10 s after a start (away from the station's
  start burst and the player's TLS handshake) and every 10 minutes. If it disagrees with the media resampler, it
  is read once more (the station may have changed format in between, which the pipeline follows); a second
  disagreement is silenced at once (`hard_stop_cb_`) and restarted cleanly, up to 3 times per stream, then the
  stream stops with an error.

**After the radio**: `stop_media_pipeline_()` sets `media_stop_pending_`, cleared once the media chain has stopped.
A stored sound (`play_slot()`: the athan, the tawashih, a preview from flash) waits for it in `pump_starts_()` (at
most 3 s): the radio's last half second is still in the resampler, mixer and I2S buffers and would play under the
new sound's start. Tones and the tick do not wait (the tick plays over the radio).

**End of a sound**: ESPHome's mixer ends a source only when the I2S speaker has reported all of its frames played
(`pending_playback_frames_`). When the I2S speaker restarts on its own ("Event/record queues desynced", "Partial DMA
write broke buffer alignment": its writer missed the 50 ms of DMA), the frames it held are dropped but stay counted
(`frames_in_pipeline_`); the next sound's mixer input starts with that count as its delay and never empties, and
the player keeps announcing until other audio plays. On the prototype every athan started over a running speaker
did this, and the athan screen stayed to `make_athan`'s 7 min timeout. The cause: the announcement reader fills its
ring buffer from flash in one `xRingbufferSend`, which ESP-IDF copies inside a critical section; at ESPHome's 1 MB
`buffer_size` that is far longer than 50 ms with interrupts off. `buffer_size` 128 KB fixed it (0 restarts in 6
starts, 3 of 3 before); athan.yaml now uses 48 KB, for the Wi-Fi (`firmware_rules.md` 3.1). As a bound, the end tracking in `loop()` stops a stored sound still announced 5 s
past its length (`AudioSlots::duration_ms`).

**Instant stops**: `hard_mute` mutes the I2S speaker and calls `begin_drain_watch()`, which notes the pipelines
whose resampler runs. `loop()` counts each resampler's stops (`media_stops_`, `announce_stops_`); `audio_drained()`
is true once each noted pipeline has stopped. A resampler stops only after its mixer input has, which stops once its
frames have played (or the I2S speaker stopped), so the unmute (`audio_drained()` or the I2S speaker stopped, at most
2 s) leaves no tail and does not cut what starts next.

**Download** (`DOWNLOAD_URL`):
1. Download into PSRAM. A `Content-Length` over the limit is refused before reading, and the read aborts as
   soon as it passes the limit. The selected entry is not downloaded at all (`selected_entry()`).
2. `mp3_scan()` needs three chained Layer III frames to sync (an ID3v2 tag is skipped). The duration is the sum
   of the frame durations.
3. Size and duration are checked against the slot's limits.
4. Handshake (section 4), erase, 4 KB bounce writes with progress, header last (with the link's CRC; an upload stores 0).

Any failure frees the buffer and leaves the slot as it was. A power cut during the write leaves no valid header,
so the slot is empty and the default comes back automatically.

**Upload** (`POST /audio/upload?slot=N`, multipart): `upload_begin` allocates limit + 1 bytes of PSRAM and claims
`sound_busy_`. `upload_data` appends, marking `overflow` past the limit. `upload_end` either reports "too big"
or queues `COMMIT_BUFFER`, which runs steps 2–4 above.

## 6. Lists and radio

- **Catalog** `docs/audio/catalog.json` (≤ 64 KB, over 1,000 entries in all): lists `athan`, `fajr`,
  `tawashih`, `tick`, each any number of `{name, url}`, numbered in file order on the clock and the web page
  (`numbered()`). A relative URL resolves against `<data_url>/audio/`. It is fetched at boot, then every 12 h
  (retry every 1 min on failure), and kept in RAM only: `CatalogList` (PSRAM), each entry with its full link and
  that link's source CRC.
- **Stations** `docs/radio/stations.json` (≤ 16 KB): 10 `{name, url}`, kept in memory (`stations_`). Fetched at
  boot and every 6 h (retry every 1 min until a first copy exists, then every 30 min), and early when a
  subscribed station gives up after 6 reconnects (about a minute) (at most every 10 min, `stations_fetched_at_`). `radio_play()`
  uses the copy in memory at once; only without any copy it queues `STATION_PLAY` (fetch, then play; a waiting
  one is replaced, not doubled, `enqueue_(job, true)`). The yaml labels the Radio Station select options from it
  (fixed buffers, rewritten in place; the select is synced by index).
- **`radio_play(slot, subscribed)`:** a subscribed slot plays the link from the list in memory (or queues
  `STATION_PLAY` when no list was ever loaded), an own slot plays the slot's `text` entity (`radio_url_N`). Each play bumps `radio_token_`, so a stale answer from an older request is
  dropped.
- **`radio_tick_()`:** sound means the media speaker runs, never the player's state. A link that fails leaves the
  player idle at once (`components/speaker`, ATHAN PATCH 2: upstream reopens it many times a second, for ever), and
  it tries again up to 6 times, 1, 3.5, 6, 8.5, 11 and 13.5 s after each start; a link that stays open without sound
  gets 20 s since it was last heard (a format restart's half second of silence is no reason). Then it gives up with
  "station not reachable". While a sound is saved the radio is paused, not retried (`radio_paused_write_`).
- **`net_watch_()`** (every loop): the network gone stops a stream at once (a streamed preview ends; the radio
  waits, "waiting for Wi-Fi") and the radio starts again when it is back, or goes off after 10 minutes.
  `radio_play()` without network waits the same way.
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
| CURRENT | Current year not stored, or Refresh pressed | 404: 30 min if nothing to stand in, else 6 h · invalid: 30 min · network: 2 min, 20 s while no times at all are loaded |
| PREVIOUS | Current year 404 and no previous year stored (new device in January) | network: 5 min · otherwise 6 h |
| NEXT | December and next year not stored | 404/invalid: 24 h · network: 1 h |

**`today_times()`:** the stored current year if present. Otherwise the stand-in from the previous year: the same
month/day (29 Feb → 28 Feb), each value converted through UTC with that date's offsets in both years, clamped
to 00:00–23:59.

**The yaml side:**
- `load_today` copies the 7 times into `prayer_hours`/`prayer_minutes` (persisted: the screen shows them, struck
  through, after a power cut until SNTP answers) and runs `compute_coming_prayer` (the first prayer at or after now).
- The 1 s interval fires the athan when the next prayer's minute is reached. After Isha it waits for midnight
  before matching tomorrow's Fajr. Then `jump_to_next_prayer` runs. Each prayer is called once a day
  (`prayer_called`, persisted): a reload of the times in its minute, or a restart, finds it again.
- Pre-Fajr Tawashih fires at Fajr − `prefajr_offset_min`, or up to 5 min late (a missed minute, a clock that
  jumped), never at or after Fajr, once per Fajr (`prefajr_called`, persisted).

## 8. The yaml

**Persisted globals (NVS, keyed by id):** `volume_level`, `fajr_volume_level` (-1 = copy from volume at boot),
`athan_enabled[7]` (indices 0, 3, 4, 5, 6 used), `buttons_locked`, `clock_12h`, `htick_enabled`,
`htick_start_hour`, `htick_end_hour`, `prefajr_enabled`, `selected_location_key` (string, max 31),
`radio_subscribed[10]`, `radio_slot`, `screen_brightness`, `screen_night`, `night_from_hour`, `night_until_hour`,
`screen_colour` (tft.yaml); today's times and the next prayer (`prayer_hours[7]`, `prayer_minutes[7]`,
`next_prayer_index/hour/minute`), what was called (`prayer_called`, `prefajr_called`) and `relay_by_prefajr` (the
relay is on because of Pre-Fajr: on_boot switches it off). The ten `radio_url_N` text entities persist themselves
(`restore_value: true`). The relay uses `RESTORE_DEFAULT_OFF`.

**Scripts:**

| Script | Does |
|---|---|
| `apply_volume`, `apply_fajr_volume` | Set the player volume. Owner % → 10 % = −`volume_range_db` dB (30) rising evenly to 100 % = 0 dB, 0 % silent; converted to ESPHome's player value, which its I2S speaker turns into −49 dB × (1 − v) |
| `play_tone_click`, `play_tone_volume` | Play the built-in tones, not over an athan |
| `check_fajr_sound` | Sets `fajr_sound_on`: the Fajr athan, the Pre-Fajr Tawashih (`prefajr_playing`) or a Fajr/Tawashih preview (`preview_list()` 1 or 2) plays. The one "which volume" test |
| `apply_playing_volume` | The volume for whatever plays now: Fajr volume when `check_fajr_sound` says so, else normal. Every "restore" uses it |
| `volume_step(delta)` | Left/Right on the clock: ±10 % on the volume of what plays, stopping at 10 %; pop-up; a tone when nothing plays |
| `show_toast(kind)` | The clock's 2 s pop-up (`toast_kind`: 1 volume, 2 Fajr volume, 3 relay) |
| `ui_key(button)` | Every key (`athan::MenuKey` 0–4): lock, clock-screen keys, or `menu.key()`. The only place keys are decided |
| `menu_setup` | Adds the menu's rows at boot, each defined once (`firmware_rules.md` 2) |
| `stop_sound` | Stop the athan, tawashih, tick or a preview, not the radio; silent at once when no radio plays |
| `radio_off` | Radio off on purpose (Up, the menu, web Stop Radio): silent at once, no automatic resume |
| `fajr_volume_feedback` | Fajr volume changed (menu or web): tone at the new Fajr level, then `apply_playing_volume`; while a Fajr sound plays it just takes the new level |
| `amp_wake` | Amp on ahead of a sound and starts `amp_idle_off`, so a sound that never starts (no radio link, empty tawashih or tick slot) cannot leave it on |
| `amp_idle_off` | Amp off 5 s after the player goes idle |
| `hard_mute` | Instant silence for a stop the owner asked for: mutes the I2S speaker (applied as audio leaves its 500 ms buffer, so silent within its 50 ms of DMA), unmutes once the stopped audio has played out ("Instant stops" above, at most 2 s). Volume changes meanwhile wait (`output_muted`, `volume_pending`) |
| `silence_audio` | Stop everything, cancel the radio resume (locked keys, web Stop Audio) |
| `radio_start` | Play `radio_slot` |
| `make_athan` | Regular or Fajr slot, Fajr volume at Fajr, LED on, waits for the end (at most 5 s past the sound's length; ≤ 7 min), resumes the radio |
| `run_prefajr` | Relay on, tawashih at the Fajr volume, resume the radio; `prefajr_relay_off` turns the relay off `prefajr_relay_min` after the tawashih (or `prefajr_relay_min` + 7 min after the start, if it was stopped) |
| `play_tick` | Tick over the ducked radio |
| `check_update` | The Update row's and the web page's check: `update.check`; `update_check_state` is 1 while it runs, 2 when it got no answer (offline: ESPHome skips the check without an error) |
| `fw_check_after_start` | The firmware check after a start, 30 s after Wi-Fi first connects (once: `fw_checked_after_start`) |
| `load_today`, `compute_coming_prayer`, `jump_to_next_prayer` | Schedule |
| `sync_web_state` (1 s interval) | Publish entities only on change: values are formatted into a buffer and compared in place; the status lines are copied each second (short strings, under the component's lock) |
| `screen_refresh` (1 s interval, and after a change) | The screen's level: Brightness (0 % = dark), or at night (Night From to Until) by Screen at Night (`night_level()` in `day_window.h`): the same as by day (Normal, which never looks at the window), `screen_dim_pct` (Dim) or dark (Turned off). A screen that would be dark is lit dim for `screen_wake_s` (120 s) after a key, and while the athan or the Pre-Fajr Tawashih plays (the 120 s start when it ends); `screen_dark_in` holds the seconds left (-1 when it stays lit), redrawn each second, and the display files draw it as a tab in a corner. Brightness set to 0 % sets `screen_force_dark`: dark at once, until the next key or the athan / Pre-Fajr Tawashih. Calls the display file's `apply_screen` only when the level changes (`screen_pct_applied`; 0 = dark, which `ui_key` reads: that key press only lights the screen) |
| `apply_screen`, `menu_display_rows` | In the display file. OLED: contrast (scaled by `oled_contrast`) or panel off; no rows. TFT: the backlight light (gamma 2); the Screen Colour row |
| `update_display` | In the display file. TFT: `id(tft).update()`, which runs the display's `lambda:` (mipi_spi draws only inside `update()`, and does not clear its buffer: `auto_clear_enabled: false`); asked with each new second of the clock (a 50 ms interval watching `time()`, so the seconds change within 50 ms of the real second; a 1 s interval would keep its boot-time phase) while the clock shows. The lambda draws the whole face (filling it black first, about 50 ms) at least every `tft_ring_update_s` (30 s), when the minute changes, every second in the last minute before a prayer, and when a fingerprint of all else it shows changes; otherwise only the seconds' box (about 2.5 ms, about 1 KB sent). OLED: the whole OLED, on the next pass of the loop (`delay: 0ms`, `mode: restart`): requests made in one pass become one draw (about 23 ms of I2C each). The clock screen is the timetable: the time (`font5`, 26 px), the status marks and the weekday with the date under them, the five prayers with the next one (the calling one during the athan) in a 29-px box, its time in `font3` (Dhuhr only outlined while Sunrise or Doha is next; with Fajr highlighted the other columns move right 1-2 px, clear of its box), and a framed box at the bottom: the next prayer small and its time in `font3`, the time left large (`font1`); while the athan plays it turns white with the prayer's name and ATHAN TIME, joined to its column |

**Intervals:** 1 s schedule, 1 s `sync_web_state` (also the menu's 60 s timeout, and its redraw when `menu_changed()`), 100 ms
window-settings fingerprint (runs `sync_web_state` at once when it changes), 10 s
OLED watchdog (`displays/oled.yaml` only) (ESPHome setup for a never-initialised display; one in-place `setup()` per boot for a display that
came back). `displays/tft.yaml`: 50 ms (a frame with each new second of the clock; the ring's flash), 1 s (the web
Screen Colour select follows the menu).

**Keys and the menu:** [firmware_rules.md](firmware_rules.md) sections 1 and 2. Navigation is `components/athan/menu.h` (host-tested in
`firmware/tests/test_menu.cpp`), drawing `menu_view.h` (`menu_view.cpp` OLED, `menu_view_tft.cpp` TFT), the rows script
`menu_setup` (plus `menu_display_rows` from the display file, after Night Until). No row numbers anywhere.
The rows go round (`ring_row()`); the last, Exit (`MenuRow::exit`), closes on Select, Left or Right. Two panes: five
rows on the left by `short_title` in the medium font after an optional `RowIcon` (the one on screen in the middle, a
dotted line where the list starts again), the row's full title small at the top of the right pane over its item.
Layouts (`RowStyle`): LIST (item as large as it fits, two lines of the medium font when too wide, arrows, marks
under it, dots), CHOICES (all items side by side when they fit, the cursor's one in reversed colours: `print(...,
COLOR_OFF, ...)` over a filled box), LEVEL (bar), TOGGLES (each item's initial in the medium font with a box, filled
when on, the cursor's one framed). A single item is a button, or with a tab in its caption the label and the two
parts on lines of their own (Info: the IP address in `MenuFonts::numbers`, Roboto 11, when the medium font is too
wide). The exit row previews the clock (label: the time, caption: the next prayer). On the TFT: five rows of 48 px
with each row's `symbol` (Material Symbols) and its name (the short one when the title is wider than 60 px), the
cursor on a bar in the accent colour; on the right TOGGLES as round switches (two lines when five do not fit),
CHOICES as chips, SWATCHES as colour dots, marks as icons (check_circle, graphic_eq).

**Wi-Fi:**
- No network is compiled in: `wifi: ap:` (AthanFallbackHotspot / athan404, `ap_timeout: 3min`) +
  `captive_portal` + `esp32_improv` (authorizer = Select, 1 min, `wifi_timeout: 15s`, LED as status).
- BLE is disabled on Wi-Fi connect and enabled on disconnect. `esp32_ble: use_psram: true`.
- The setup screen reads `id(improv_ble).get_improv_state()`. `on_state` redraws it.

## 9. Web

- ESPHome's page (`web_server` v3) with sorting groups: Now, Athan, Athan On/Off per Prayer, Sounds, Radio,
  Radio Stations, Location and Prayer Times, System.
- `/audio` (custom handler): `GET /audio` renders the page. Its sticky status bar is an iframe named `st` showing
  `GET /audio/status?g=`, and every form posts into it (`target="st"`), so an action never reloads or scrolls the
  page. Each sound is one form: a `<select name="entry">` with the whole list (Random and Custom first where they
  apply, then "1. Athan 1", …; the sound in use preselected with ✔), a **Preview** button (`formaction`) and
  **Download**, so the section stays the same size however long the list is. `POST /audio/download|preview?list=&g=`
  with `entry` in the form's body (`arg()` reads the body and the link) queue the action and redirect to the bar;
  `entry` -4 (`MENU_RANDOM`) and -1 (`MENU_CUSTOM`) download nothing (`web_download_()`: Random is chosen, or the
  bar says what is in use), and Preview on Random plays a random entry. `POST /audio/stop` stops what Up, Select or Down stop while a sound plays (the yaml's `stop_sound`, through
  `set_stop_sound_callback`): a preview, the Pre-Fajr Tawashih, the athan or the tick, silent at once. The radio
  plays on (web Stop Radio).
  `POST /audio/upload?slot=&g=` is a multipart upload. Every action redirects the bar to `/audio/status?g=&a=1`:
  the main loop carries the action out a moment after the bar is drawn, so `a=1` makes the bar load once more a
  second later (`render_audio_status(gen, acted)`), and it shows the action's result ("already downloaded and in
  use", "Stopped"). The bar refreshes itself every 2 s while a download or upload is busy. `g` is `sounds_changed_` when the page was drawn: once a slot changed, the bar offers
  **Reload page**. Plain HTML, no JavaScript.
- ESPHome's page loads one script, `firmware/web_audio_link.js` (`web_server: js_include`, served as `/0.js`). It
  underlines the **Change Sounds At** value and opens `/audio` on click, without replacing ESPHome's elements.
- Firmware updates: `update: platform: http_request` with the Releases manifest, `update_interval: never`. The
  clock checks 30 s after Wi-Fi first connects after a start (`fw_check_after_start`; ESPHome's own check after a
  start gives up when Wi-Fi takes over a minute), daily at 03:00 (`time: on_time`), and when asked (the Update menu
  row, the Check For Update button). It never installs by itself.
  - An update found shows as the update mark on both screens while online (`update_ready` in `displays/*.yaml`, in
    the TFT face's fingerprint; `on_update_available` redraws at once).
  - The menu's Update row reads `fw_update`'s own state: **Install x.y.z** while one is available, Installing while
    it installs. `ota: http_request` records the version of an install that failed (`fw_failed_version`, set by
    `on_error`, cleared by `on_begin`) for the row's caption and Firmware Version.
  - The install runs on the main loop (ESPHome's `OtaHttpRequestComponent::flash()`): nothing else runs until the
    restart, so a prayer whose minute passes meanwhile is not called after it.
- `ota:` has `esphome` (encrypted, same key as the API), `http_request` and `web_server` (needed for multipart
  uploads to reach custom handlers).

## 10. Checks you can run without hardware

`firmware/tests/run_tests.sh`:
- `partitions.csv`: overlaps, alignment, 16 MB, and ESP-IDF's own `gen_esp32part.py` when a build has downloaded
  it; the component's sound regions (ascending, disjoint, 4 KB aligned, each holding its 4 KB header and the largest
  file `audio_slots.h` allows), the prayer slots (a leap year fits), and the partitions it looks up by subtype and
  name.
- Calendar helpers.
- The POSIX TZ parser against Python `zoneinfo`, every 30 min over 2025–2028 for PST8PDT, Cairo, Berlin and
  Riyadh (280,512 vectors).
- With MP3 paths as arguments (relative to the current folder or absolute), `mp3_scan` against `ffprobe`.
- The device menu's navigation (`test_menu.cpp`).
- The day windows, `night_level()` and other helpers (`test_helpers.cpp`); every `sym::` icon code point in the TFT's
  icon fonts.
- The TFT's ring drawing (`test_tft_draw.cpp`, against a stand-in `display.h` in `tests/host_display/`): `arc()` and
  `ring()` draw the same pixels as a plain scan that works out the angle of every pixel, for 5,820 rings and arcs.
- `docs/` (`check_docs.py`): the catalog (at most 64 KB, names ASCII and at most 12 characters, no duplicate links,
  relative links that exist, the slot limits; sample rate and stereo as warnings), the 10 stations, the mosque keys
  (mosques.json = the Location options = guides/using.md section 7 = the folders) and every yearly file (its structure, plus the
  day-to-day and year-to-year warnings). Errors fail the run, warnings are listed.
- With ArduinoJson on the computer (a build_firmware.sh build, syntax_check.sh's download, or `ARDUINOJSON=`):
  the clock's own `parse_year_file()` on every yearly file and its time zone parser against the scripts'
  (`test_year_files.cpp`, stand-ins in `tests/host_prayer/`); skipped with a message otherwise.

`firmware/tests/syntax_check.sh`:
- Runs once per display (`display_type` oled, then tft; `DISPLAYS=tft` for one).
- Generates the C++ with `esphome compile --only-generate` on a temp copy (placeholder tones if
  `firmware/sounds/` is empty). `ESPHOME_DATA_DIR` is unset, so nothing lands in a real build; the generated folder
  is found whatever `device_name` is, and a missing `main.cpp` or component source fails the check.
- Type-checks the component sources (`-Wall -Wextra`) and `main.cpp` (all lambdas) with `clang++
  -fsyntax-only` against the real ESPHome headers. `idf_stubs/` stands in for ESP-IDF and library headers; a header
  with no stand-in fails the check (clang would otherwise stop there and pass everything after it).

## 11. To verify on the prototype

The full acceptance list is `guides/building.md` section 7. These points in this implementation rest on assumptions that
only hardware can confirm:

- Firmware size against the 0x360000 app slots, and internal heap with BLE + hotspot + an athan.
- `play_file()` of a memory-mapped `AudioFile` from the `audio` partition (the mmap stays valid while playing;
  `invalidate()` refuses a playing slot).
- How the player reports state while an announcement plays over the radio. `slot_playing()` relies on
  `ANNOUNCING`. Seen on the prototype (2026-10-15): the player's own state is ANNOUNCING (its log says so, and the
  past-its-length bound, which needs it, fired), while the API reports an announcement as PLAYING.
- `esp_http_client` redirects for `releases/latest/download` (handled by ESPHome's `update` component) and for
  raw.githubusercontent.com (no redirect normally).
- Stream reconnect behaviour after a router restart.
- The MAX98357A gain jumper and the 4.7 kΩ SD_MODE resistor, left channel (`max98357a_amplifier.md`).
- Whether the time survives a software restart (an update, the Restart button) before SNTP answers: ESP-IDF keeps
  it across software restarts, but not across a power cut.

## 12. Ideas for later

None of these is needed; each is independent. Hardware options are in `HARDWARE.md` section 10.

| Idea | What it gives | What it takes |
|---|---|---|
| **Atomic sound replacement** | No empty slot if the power fails while a sound is written (today the default downloads again) | A fourth 3 MB spare region: write the new sound there, then swap which region the slot uses (a small map in NVS). The app slots would shrink to about 1.9 MB each, barely above today's 1.8 MB firmware, so in practice it needs 32 MB flash |
| **A built-in fallback athan** | A real athan, not three tones, when a slot is empty | A short low-bitrate athan in the firmware (`files:`, about 0.5 MB at 16–24 kbps mono); it costs that space in both app slots |
| **Factory audio image** | New clocks ship with the default sounds, no first download | Build the four regions and their headers on a computer (a small script), flash them with esptool at the `audio` partition's offset together with the firmware |
| **Browser installer** (ESP Web Tools on GitHub Pages) | Flash a new clock and set its Wi-Fi from Chrome over USB, no ESPHome install | `improv_serial` in the yaml, a manifest and a page under `docs/`; on this board it goes through J6 and a USB-serial adapter (the USB-C is power only) |
| **More upload formats** (Opus, FLAC) | Owners can upload what they have | Duration parsing per format in `mp3_check`; ESPHome already has the decoders. WAV stays out: 5 min would not fit a 3 MB slot |
| **Names for own radio links** | The screen shows a name for a slot that plays its own link (it says "own link" now) | One more text entity per slot, or a "name, then link" field format |
| **AAC and HLS stations** | Stations that are not MP3, Opus, FLAC or WAV | Home Assistant's transcoding proxy where HA is used; natively a different audio library (ESP32-audioI2S, Arduino, not ESPHome) |
| **A CDN fallback for GitHub** (jsDelivr) | Keeps working if raw.githubusercontent.com rate-limits or is blocked. A clock makes one prayer-times request a year and a few list and sound requests, far below GitHub's limits | A second base URL, tried after a failure; jsDelivr serves GitHub repositories over HTTPS |
| **One audio controller** (owner's choice: later) | The rules of who plays what in one place. Today they are spread over about 20 yaml scripts (`make_athan`, `run_prefajr`, `play_tick`, `stop_sound`, `silence_audio`, `radio_start`/`radio_off`, `hard_mute`, the volume scripts) and about 25 flags (`athan_playing`, `prefajr_playing`, `radio_resume`, `menu_radio_resume`, `preview_list_`, `tawashih_stream_`, …), and each sound checks the others: the 2026-10-15 review found several bugs where one place missed a rule (the tick over the Random tawashih, the athan losing a radio a menu preview had paused, the keys and `/audio` Stop stopping different things) | A small C++ API in the component (play a sound, stop, stop all, radio on/off, what plays) that owns radio pause and resume, the volume per sound, mute and drain, ducking, the amp and end detection; the yaml only calls it. A rewrite of working code: worth it before more audio features, checked with the on-device tests |
