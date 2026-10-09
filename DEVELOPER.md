# Developer notes (version 3 firmware)

How the firmware is put together, for anyone changing it. Owner-facing behaviour is in `README.md`, the design
reasoning in `version3_planning.md`, and the short list of rules in `CLAUDE.md`.

## 1. Files

| Path | What |
|---|---|
| `firmware/athan.yaml` | All behaviour: entities, menus, schedule, OLED, scripts, intervals (ESPHome + inline C++ lambdas) |
| `firmware/partitions.csv` | 16 MB layout: nvs, otadata, app0/app1 (0x360000 each), `prayer` (0x41, 128 KB), `audio` (0x40, 0x910000) |
| `firmware/components/athan/__init__.py` | Component schema: `media_player`, `media_speaker` and `announcement_speaker` (the two resamplers, for clean starts and format checks), `time_id`, `data_url`, `radio_urls` (list of `text` ids). Requests the MP3 decoder, the certificate bundle, `esp_http_client` and `esp-tls` |
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
   3. Starts the worker task (`athan_worker`, 10 KB internal stack, priority 1: the same as ESPHome's main loop and
      the audio pipeline's tasks, which a TLS handshake at a higher priority held off for hundreds of ms).
   4. Registers the `/audio` handler on `web_server_base`.
2. `esphome: on_boot` (priority -100):
   1. Copies `volume_level` into `fajr_volume_level` the first time (when it is -1).
   2. Calls `set_location(selected_location_key)`.
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

Nothing in a setup-time trigger may draw or play (`CLAUDE.md`). Template switches use `restore_mode: DISABLED`.

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
| `download_from_catalog(list, entry)` | Downloads → checks → writes; progress in `sound_status()`. The selected entry is never downloaded again ("already selected"); downloading another entry first fetches it anew |
| `selected_entry(list)` | Index of the list entry (the selected one) whose link matches the slot's stored source CRC (version 1 headers: the label), -1 for an upload or an unlisted sound |
| `sound_busy()`, `sound_status()` | One download/upload at a time |
| `fetch_catalog()`, `catalog_ready()`, `catalog_size(l)`, `catalog_name(l, e)` | Suggested lists |
| `has_custom(list)` | The slot holds a sound that is not a list entry: an upload (source 0), or a download the loaded list no longer has. Shown as "Custom" in the menu and on `/audio` |
| `menu_size(l, off)`, `menu_item(l, off, i)`, `menu_index(l, off, item)` | The OLED sound list: `MENU_OFF` (tick only), `MENU_RANDOM` (tawashih, first), `MENU_CUSTOM` (while `has_custom`), then the entries. `MENU_NONE` for an index with nothing there |
| `tawashih_random()`, `set_tawashih_random(on)` | The Random tawashih (default, saved as a preference). On frees the stored tawashih (`CLEAR_SLOT` once nothing plays from it); a download or upload started after the choice turns it off |
| `play_random_tawashih()`, `tawashih_streaming()`, `stop_tawashih()` | Pre-Fajr with Random: streams a random entry (not the last one), one retry with another entry if it fails at once; false without internet or list |
| `has_stored_times()` | The location has stored prayer times: the timetable shows the last day's times and the next prayer (saved globals) struck through until the clock is set |
| `request_preview(l, item, ms)` | How the menu and `/audio` start previews: stops the current preview at once (one STOP), starts the new one (`preview_catalog` or `preview_stored`) after `ms` without another request. Menu Left/Right use 500 ms, reaching a sound row 800 ms, `/audio` 300 ms. Keeps fast browsing to one stream and a few media commands (CLAUDE.md, "Never send media player commands in bursts") |
| `preview_stored(list)` | Plays the stored sound from flash as a preview (the Custom item; `/audio/preview?entry=-1`) |
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

Also from lambdas: `athan::draw_menu(display, menu, {large, medium, small})` (`menu_view.h`) draws the open menu.

## 4. Threading

- **Main loop:** the yaml, `loop()`, the media player calls, entity publishing, the stored-sound map.
- **Worker task:** a FIFO of `Job`s (`CATALOG`, `STATIONS`, `STATION_PLAY`, `DOWNLOAD_URL`, `COMMIT_BUFFER`,
  `PRAYER_YEAR`). It does HTTPS (`http_get_`: crt bundle, manual redirect loop up to 5 hops, 20 s timeout, size
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

**Regions** (inside the `audio` partition):

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
starts, 3 of 3 before); athan.yaml now uses 48 KB, for the Wi-Fi (CLAUDE.md). As a bound, the end tracking in `loop()` stops a stored sound still announced 5 s
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

- **Catalog** `docs/audio/catalog.json` (≤ 64 KB): lists `athan`, `fajr`, `tawashih`, `tick`, each ≤ 10
  `{name, url}`. A relative URL resolves against `<data_url>/audio/`. It is fetched at boot, then every 12 h
  (retry every 1 min on failure), and kept in RAM only.
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
  it tries again 3, 6 and 9 s later; a link that stays open without sound gets 20 s. Then it gives up with "station
  not reachable".
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
| `apply_volume`, `apply_fajr_volume` | Set the player volume. Owner % → 10 % = −`volume_range_db` dB (30) rising evenly to 100 % = 0 dB, 0 % silent; converted to ESPHome's player value, which its I2S speaker turns into −49 dB × (1 − v) |
| `play_tone_click`, `play_tone_volume` | Play the built-in tones, not over an athan |
| `check_fajr_sound` | Sets `fajr_sound_on`: the Fajr athan, the Pre-Fajr Tawashih (`prefajr_playing`) or a Fajr/Tawashih preview (`preview_list()` 1 or 2) plays. The one "which volume" test |
| `apply_playing_volume` | The volume for whatever plays now: Fajr volume when `check_fajr_sound` says so, else normal. Every "restore" uses it |
| `volume_step(delta)` | Left/Right on the clock: ±10 % on the volume of what plays, stopping at 10 %; pop-up; a tone when nothing plays |
| `show_toast(kind)` | The clock's 2 s pop-up (`toast_kind`: 1 volume, 2 Fajr volume, 3 relay) |
| `ui_key(button)` | Every key (`athan::MenuKey` 0–4): lock, clock-screen keys, or `menu.key()`. The only place keys are decided |
| `menu_setup` | Adds the menu's rows at boot, each defined once (CLAUDE.md "Keys and the menu") |
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
| `check_update` | `update.check`, then sets `update_check_state` for the menu |
| `load_today`, `compute_coming_prayer`, `jump_to_next_prayer` | Schedule |
| `sync_web_state` (1 s interval) | Publish entities only on change; unchanged values are compared in a buffer, without allocating |
| `update_display` | The whole OLED, on the next pass of the loop (`delay: 0ms`, `mode: restart`): requests made in one pass become one draw (about 23 ms of I2C each). The clock screen is the timetable: the time (`font5`), the date and status marks, the five prayers with the next one (the calling one during the athan) in a box, and a framed bottom row with the time left, inverted to "ATHAN TIME" and joined to its column while the athan plays |

**Intervals:** 1 s schedule, 1 s `sync_web_state` (also the menu's 60 s timeout, and its redraw when `menu_changed()`), 10 s
OLED watchdog (ESPHome setup for a never-initialised display; one in-place `setup()` per boot for a display that
came back).

**Keys and the menu:** CLAUDE.md "Keys and the menu". Navigation is `components/athan/menu.h` (host-tested in
`firmware/tests/test_menu.cpp`), drawing `menu_view.h`, the rows script `menu_setup`. No row numbers anywhere.
The rows go round (`ring_row()`); the last, Exit (`MenuRow::exit`), closes on Select, Left or Right. Two panes: the
rows by name on the left (the one on screen in the middle, a dotted line where the list starts again), the row's
item on the right. Layouts (`RowStyle`): LIST (item as large as it fits, two lines of the medium font when too wide,
arrows, marks under it, dots), CHOICES (all items side by side when they fit, the cursor's one in reversed colours:
`print(..., COLOR_OFF, ...)` over a filled box), LEVEL (bar), TOGGLES (each item's initial with a box, filled when
on, the cursor's one framed). A single item is a button, or with a tab in its caption the label and the two parts on
lines of their own (Info). The exit row previews the clock (label: the time, caption: the next prayer).

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
  page. `POST /audio/download|preview|stop|random?list=&entry=&g=` queue an action and redirect to the bar
  (`random`: the Random tawashih).
  `POST /audio/upload?slot=&g=` is a multipart upload. The bar refreshes itself every 2 s while a download or
  upload is busy. `g` is `sounds_changed_` when the page was drawn: once a slot changed, the bar offers
  **Reload page**. Plain HTML, no JavaScript.
- ESPHome's page loads one script, `firmware/web_audio_link.js` (`web_server: js_include`, served as `/0.js`). It
  underlines the **Change Sounds At** value and opens `/audio` on click, without replacing ESPHome's elements.
- Firmware updates: `update: platform: http_request` with the Releases manifest (every 6 h, the Update menu
  item, the Check For Update button). `ota:` has `esphome` (encrypted, same key as the API), `http_request` and
  `web_server` (needed for multipart uploads to reach custom handlers).

## 10. Checks you can run without hardware

`firmware/tests/run_tests.sh`:
- `partitions.csv`: overlaps, alignment, 16 MB, the component's region sizes, and ESP-IDF's own
  `gen_esp32part.py` when a build has downloaded it.
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
- Stream reconnect behaviour after a router restart.
- The MAX98357A gain jumper and the 4.7 kΩ SD_MODE resistor, left channel (`max98357a_amplifier.md`).
