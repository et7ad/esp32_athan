# Firmware rules

The rules the version 3 firmware depends on, each with the reason behind it. Most were learned on the prototype:
breaking one brings back a bug that was seen there. Read this before changing anything in `firmware/`, and change
it when a rule changes. How the firmware works is in [DEVELOPER.md](DEVELOPER.md); this file is what must stay
true.

1. [Keys](#1-keys)
2. [The device menu](#2-the-device-menu)
3. [ESPHome](#3-esphome)
4. [Wi-Fi](#4-wi-fi)

## 1. Keys

Five keys (5-way switch: `ui_up`, `ui_down`, `ui_left`, `ui_right`, `ui_select` = centre push), all going to script
`ui_key` with `button` = `athan::MenuKey` (0 Up, 1 Down, 2 Left, 3 Right, 4 Select). `ui_key` is the only place
that decides what a key does, in this order:

1. **Screen dark** (Brightness 0 % or Screen at Night: Turned off, `screen_pct_applied == 0`): the press only lights
   it, dim (`screen_refresh`: lit `screen_wake_s`, 120 s, after the last press, `screen_dark_in` counting the
   seconds down for the display files' corner tab; setting 0 % sets `screen_force_dark`, dark at once until a key or
   the athan). This comes before everything else, the lock included.
2. **Locked** (`buttons_locked`): every key only runs `silence_audio`.
3. **Menu open:** `menu.key()`.
4. **Clock screen:**
   - Left/Right = `volume_step`: the volume of what plays (`check_fajr_sound` decides which), never stops
     anything, stops at 10 %, pop-up via `show_toast`.
   - While the athan, the tawashih, the tick or a preview plays, Up/Down/Select = `stop_sound`: it stops that sound,
     the radio plays on. The `/audio` page's Stop does the same (`set_stop_sound_callback`).
   - Otherwise Up = radio on/off (`radio_start` with `radio_slot` / `radio_off`), Down = relay + pop-up,
     Select = `menu.open()`.
   - On the Wi-Fi setup screen (no valid time, improv active, no Wi-Fi), Select only authorises Bluetooth setup
     (`esp32_improv` `authorizer: ui_select`).

At power-up: Select held = forget Wi-Fi (on the device only: the owner does not want it on the web page); any
direction held = unlock (the web switch Buttons Locked).

- Don't add long-press or timing logic.
- Everything else the menu and the keys can do must also exist on the web page (the sounds on `/audio`): keep it
  that way when adding a row or a key action.
- All five keys have a `settle: 40ms` filter: contact bounce otherwise reads as extra presses (section 3.1).

## 2. The device menu

The menu is two wheels: Up/Down = rows, going round (no end returns to the clock); Left/Right = the row's items.
The way back is the last row, Exit (`MenuRow::exit`: Select, Left and Right close; its pane previews the clock),
Select on an item that closes, or 60 s without a press (the 1 s interval).

**Drawing** (`components/athan/menu.h`, drawn by `menu_view.h` in two panes):
- OLED: five rows on the left by `short_title` in the medium font after an optional `RowIcon` (clock: the tick
  rows; speaker: Athan On/Off and the volumes; sun and moon: the screen rows; lock; the exit arrow); the row's full
  `title` small at the top of the right pane over its items.
- TFT: every row has its `symbol` instead, and the full title where it fits. Every row sets a `symbol`
  (`tft_theme.h` `sym::`); its code point must be in `tft.yaml`'s icon fonts, which `run_tests.sh` checks.
- A short title must fit the left pane with its icon: at most 53 px in Roboto 14 from its start ("Volume" with the
  speaker does not, so it is "Vol").

**The rows** are defined once each in script `menu_setup` (run at boot), top to bottom: Radio, Athan, Fajr Athan,
Tawashih (Random first), Pre-Fajr, Hourly Tick, Tick From, Tick Until, Athan On/Off, Volume, Fajr Volume, Location,
Clock, Brightness, Screen at Night, Night From, Night Until, then the display's own rows (`menu_display_rows`:
Screen Colour on the TFT), Update, Lock Buttons, Info, Exit.
- Nothing refers to row numbers: to add, remove or reorder a row, edit `menu_setup` only.
- Sound rows go through `menu_item()` (Off, Random, Custom, entries labelled with `numbered()`), never index
  arithmetic.

**A row** is a `MenuRow`: `count`/`label`, optional `active` (check mark, where it opens), `playing`, `caption` (a
tab splits it into two lines: Info), `id_at`/`find` (keeps the cursor on its item when the list changes), and the
hooks `show` (item on screen: previews), `apply`, `leave` (stop the preview).
- Kinds:
  - CHOICE: Select takes the item; on the item in use it closes.
  - VALUE: Left/Right apply at once; Select closes. The row follows a value changed elsewhere (the web page).
  - ACTION: Select always runs `apply`; true = close.
- Styles (`RowStyle`, layout only):
  - LIST;
  - CHOICES: all items side by side when they fit the pane (Pre-Fajr); Clock and Location fall back to LIST;
  - LEVEL: a bar (the volumes, Brightness);
  - TOGGLES: each item's `short_label` initial with a box, filled when `on` (Athan On/Off);
  - SWATCHES: a dot in each item's `swatch` colour (Screen Colour).

**Rules:**
- `refresh()` never calls hooks with side effects: drawing calls it.
- The menu's `on_close` restores the volume and brings back a radio that a menu preview replaced
  (`menu_radio_resume`).

## 3. ESPHome

### 3.1 Audio

- **Keep the media player's `buffer_size` small (48 KB).** Measured on the prototype with test builds, 2026-10-07/08:
  - A sound from flash starts by filling the reader's ring buffer in one copy, and ESP-IDF's ring buffer copies
    inside a critical section (interrupts off on that core). At ESPHome's 1 MB default that starved the I2S speaker
    at every athan started over a running speaker. It restarted, and the athan never ended (DEVELOPER.md section 5).
  - Wi-Fi drops at station starts: a station's server sends its first 130-260 KB at full speed and the reader takes
    as much as fits. Station switches every 8-15 s, sizes in rotating 8-minute blocks (one run, so the router's
    better and worse hours hit every size): 4 drops in 83 starts at 1 MB, 6 in 88 at 128 KB, 0 in 89 at 48 KB.
    The router (TP-Link) still drops the clock now and then in its bad hours, also in steady play, at any size.
- **Never send media player commands in bursts.** The speaker media player's command queue holds 20 and is drained
  one command per loop by the same main loop that fills it, with a blocking send (`portMAX_DELAY`; only volume
  sends are non-blocking). A full queue freezes the loop until the 5 s task watchdog resets the board: browsing
  sounds quickly did exactly that. So:
  - previews go through `request_preview()` (stops the current one once, starts the new one after a pause);
  - every STOP is sent only when something plays (`stop_media_pipeline_()`, `slot_stop_sent_`, the guard in
    `silence_audio`);
  - tones are at most one per 90 ms (`last_tone_ms`);
  - all five keys have a `settle: 40ms` filter.
- **Which volume:** the player has ONE volume for both pipelines.
  - Fajr volume = the Fajr athan, the Pre-Fajr Tawashih and Fajr/Tawashih previews (component
    `set_preview_volume_callback`, set at boot). Normal = everything else (radio, other athans, ticks, tones, other
    previews).
  - Each sound sets its own volume when it starts, and every "back to normal" goes through
    `apply_playing_volume`, never `apply_volume` directly.
  - Radio asked for during a Fajr sound waits (`radio_resume`); ticks skip previews and the Pre-Fajr Tawashih.
- **Volume curve:** ESPHome's I2S speaker applies the player volume v as −49 dB × (1 − v), so a straight
  `pct / 100` makes 50 % only −24.5 dB (owners called V3 "quiet", 2026-10-06). `apply_volume`/`apply_fajr_volume`
  map the owner's % onto `volume_range_db` (30 dB) instead. The library files average −16.4 LUFS, like radio;
  loudness is not the files.
- **`components/speaker` overrides ESPHome's.** Upgrading ESPHome means re-copying it from the new version and
  re-applying the ATHAN PATCH blocks (`check_speaker_override.py --update` rewrites `athan.patch`), or deleting it
  if upstream now re-sends a changed stream format to the speaker and stops retrying failed links.

### 3.2 Setup and entities

- **Template switches mirroring a global use `restore_mode: DISABLED`** and report state with `lambda`. Any other
  mode fires the actions inside `setup()`, before the display exists. That caused a boot loop in V2.
- **Nothing that can fire during setup may draw or play.** `update_display` guards with `id(oled).is_ready()`
  (`id(tft)`), not `is_in_loop_state()`. The OLED is optional: a dead or missing display marks itself failed, and a
  10 s interval watchdog re-inits it at most once per boot. Never restart the device because of the display.
- `Component::defer` is safe from other tasks. Entity `publish_state` is not, so call it from the main loop only.
- Multipart uploads reach custom web handlers only when `ota: - platform: web_server` is present. It stays even
  though owners rarely use the firmware upload form.

### 3.3 C++ in lambdas and the component

- **Lambda `id()` expansion:** `id(x).m()` → `x->m()`, `id(x)` alone → pointer, `id(global)` → `global->value()`.
  So `auto &d = *id(oled);`, not `auto &d = id(oled);`.
- **Select `set_action`: `x` is a `StringRef`**, so use `std::string(x.c_str(), x.size())` or
  `index_of(x.c_str(), x.size())`. `select.at(i)` returns an optional.
- **Never give an entity an id that equals a C++ namespace** (`mixer_speaker`, `speaker`, `audio`, `time`, …):
  the generated global variable shadows the namespace. The mixer is `audio_mixer` for that reason.
- `update::UpdateEntity::perform()` is overloaded in a way that hides the base, so call `perform(false)`.
- Use `millis_64()` for deadlines that can sit for weeks (a 32-bit `millis()` deadline goes stale after 24.8 days).
  32-bit `millis() - start` for short durations is fine.

### 3.4 Drawing

- **TFT:**
  - Text drawn over a colour (a chip, the cursor bar, a pop-up card) passes that colour as `print()`'s
    `background`, or its smoothed edges blend into black. The `tft::` shapes take the colour behind them for the
    same reason: the panel cannot be read back.
  - Icons are Material Symbols Rounded (`gfonts://Material Symbols Rounded`, bpp 4). An icon's square sits on the
    baseline, so draw them with `tft::icon()` by their top-left corner. Write code points as `\uXXXX` escapes,
    never the raw characters (they are invisible in an editor).
  - Fonts: `t_time` 34, `t_big` 28 (digits, `hms%ONF`), `t_item` 22, `t_head` 18, `t_row` 16, `t_num` 14
    (digits), `t_label` 13 (all Roboto Medium), `t_text` 15 and `t_small` 13 (Roboto); icons `i_16`, `i_22`,
    `i_28`.
- **OLED:**
  - Every OLED icon is drawn with primitives (`filled_rectangle`, `circle`, `filled_triangle`, `line`). In yaml
    lambdas `d.draw_pixel_at` needs its colour: the SSD1306 class hides `Display`'s two-argument overload.
  - Fonts:
    - `font1` (Roboto 20) and `font2` (Roboto 14) carry ESPHome's default ASCII glyphs.
    - `font3` (Roboto 11) has only `AMP`, the digits, `.`, `:` and `-`: AM/PM next to the time, the next prayer's
      time in the table and the bottom box, the menu's IP address (`MenuFonts::numbers`). A 24-hour time in it is
      27 px, too wide for a 25-px table column, so only the highlighted column uses it.
    - `font4` (Tiny5, 8 px, a Google pixel font) has all printable ASCII and `°`: the menu's titles and captions,
      the clock screen's weekday, timetable and next prayer, and the radio slot number.
    - `font5` (Roboto 26) has only the digits, `:` and `-`, for the clock screen's time.
  - Small text is a pixel font at its native size: Roboto scaled to 10 px came out ragged on the one-colour OLED
    (Roboto 11 is clean for digits). Never dither (fade) text: clearing pixels breaks one-pixel strokes.
  - `menu_view.cpp`'s layout assumes these sizes. Catalog and station names are ASCII and at most 12 characters for
    that reason.
- A character a font does not have draws as an empty box (and ESPHome logs a warning at every draw).

## 4. Wi-Fi

- **ESP-IDF's own defaults, buffers in internal RAM.** `network: enable_high_performance: false`, plus six
  `CONFIG_ESP_WIFI_*` / `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP` lines in `sdkconfig_options`, because ESPHome's
  Wi-Fi component ignores that switch. The router dropped the clock only while the radio played with ESPHome's
  streaming profile (PSRAM buffers, 32-frame block-ack windows). Don't bring it back without a long radio test.
  - Exception: `network: tcp_send_buffer: 16384` (what the clock sends: logs, web, API). At ESP-IDF's 5.7 KB a log
    burst made ESPHome drop the API/log connection.
- **802.11b/g only** (`athan: wifi_bg_only: true`, `esp_wifi_set_protocol` at every station start). The router
  (TP-Link) dropped the clock with reasons 3, 6 and 7, mostly at station starts; turning off 802.11n is the fix
  reported for ESP devices on TP-Link routers. ESPHome has no such option on the ESP32.
- **Transmit power follows distance** (`tune_tx_power_()`, every 10 s): 13 dBm while the router is heard stronger
  than -40 dBm, 20 dBm again below -50 dBm. Right next to the router, full power reaches it at the strongest signal
  802.11 receivers are specified for, and drops ("Not Authenticated") clustered at station starts at -23 dBm.
- **Modem sleep is off while Bluetooth is off** (`set_wifi_low_latency()` after `ble.disable`, back on before
  `ble.enable`): ESP-IDF needs it with Bluetooth, and without it streams start faster. Bluetooth comes on only
  after Wi-Fi has been gone 15 s (`ble_when_offline`): routers drop clients for seconds now and then.
