# Athan clock version 3: planning

Date: 2026-10-04, updated 2026-10-05. Status: **firmware 3.0.0 implemented** in this repository
(`et7ad/esp32_athan`), type-checked and unit-tested on a computer, **not yet run on hardware**. The breadboard
prototype (section 13) comes next, then the PCB (`HARDWARE.md`). Section 0 lists where the implementation
differs from the plan below.

Version 3 is the third board (after `athanV1` and `athanV2`, whose KiCad files stay in the V2 repository
`et7ad/esp_athan`). It replaces the ESP8266 and the DFPlayer with an ESP32-S3 that plays audio itself. Deployed
ESP8266 devices keep running the V2 firmware unchanged.

Version 3 has **its own GitHub repository**, this one. The V2 repository stays, still maintained, for the V2
(ESP8266) devices, and its README points new builders here. Details in section 9.3.

In the plan below, "today" and "the current firmware" mean the V2 firmware in `et7ad/esp_athan`.

## 0. Implementation status (firmware 3.0.0, 2026-10-05)

Implemented: everything in sections 4–10. Where the build differs from the plan text, the build wins:

| Topic | Plan said | Implemented |
|---|---|---|
| Files | `firmware/athan_v3.yaml`, component `athan_audio` | `firmware/athan.yaml`, component `firmware/components/athan/` (`DEVELOPER.md`) |
| Radio defaults (7) | A "radio" list in the catalog and a **Reset Radio Stations** button | `docs/radio/stations.json` (exactly 10 stations) and a per-slot switch **Radio N follows project list** (on by default). As built, the list stays in memory (fetched at boot and every 6 hours), so changing a link there reaches every following clock within 6 hours. Off = the slot's own link (`Radio N own link`). The OLED shows the station's name from the list |
| Catalog (5) | Absolute URLs, a `radio` list | Lists `athan`, `fajr`, `tawashih`, `tick` only; a `url` may be relative to `docs/audio/`. Clocks re-read it every 12 h |
| Preview (6.1) | 20 s | Plays until stopped, Left/Right move on, or a choice is made. The selected entry plays from flash, not the internet. A sound list opens on the selected entry and plays it. Downloading the selected entry again does nothing |
| Location | Index persisted | The **key** is persisted (`selected_location_key`), so the Location list may be reordered or extended freely |
| Generator (9.2, 9.3) | One script writes the V3 yearly file and the V2 daily files | Every script here writes **only** the V3 yearly file (one positional list per day). V2 daily files are produced in the V2 repository with its own scripts. `make_yearly_json.py` converts years that exist only as V2 daily files |
| Built-in tones (4.1) | `audio_file` | `media_player: files:` from `firmware/sounds/click.mp3` and `volume.mp3` (V2's C3 and C2), added by the builder |
| Stand-in mark (9.2) | Shape open | A small hollow square after the next prayer's time; "[estimated]" on the web page |
| Wi-Fi setup screen (8.1) | Text open | `Wi-Fi setup:` / `BT: press Select` (or `allowed`, `joining..`) / `Hotspot: on` or `soon`. On that screen a Select press only authorises Bluetooth |
| Radio menu (7) | Select plays or stops | Select plays or stops **and returns to the clock**. All ten slots are listed (an empty one says "No link here"). On the clock, Up plays the slot chosen last, or stops the radio |
| Buttons (3.1, 6.2) | Two buttons, Next and Select; a list menu | **One 5-way switch** (Up, Down, Left, Right, Select; owner's choice 2026-10-06). Clock screen: Left/Right = volume of what plays, Up = radio, Down = relay, Select = menu; Up/Down/Select stop the athan, tawashih, tick or a preview. The menu is two wheels (Up/Down rows, Left/Right items) with small previews of the neighbours; Tick Window became the rows Tick From and Tick Until. Power-up: Select held = forget Wi-Fi, any direction held = unlock (README 1.3, CLAUDE.md "Keys and the menu") |
| USB-C (3.1, 3.4) | 16-pin with USB data; flashing and logs over the charging cable | **6-pin power-only USB-C** (owner's choice 2026-10-05, with 5.1 kΩ CC resistors). The first flash and serial logs go through J6, a populated 1×6 UART header (GND, IO0, EN, TXD0, RXD0, 3V3; jumper 1–2 = download mode) with a 3.3 V USB-serial adapter; the firmware logs on UART0. Later updates go over Wi-Fi |
| Empty slot | Download the default again at the next boot | Downloaded again automatically while online, at most every 30 min per slot (a missing default is not hammered) |

Menu rows as built (16): Radio, Athan, Fajr Athan, Tawashih, Pre-Fajr, Hourly Tick, Tick From, Tick Until,
Athan On/Off, Volume, Fajr Volume, Location, Clock, Update, Lock Buttons, Info. No Cancel item: Up from the first
row or Down from the last returns to the clock.

Still open: the sound files and their names in `docs/audio/catalog.json` (placeholders now), the F8 choice in
5.1, and everything in section 13.

The baseline is deliberately simple and efficient: no line-out jack, no RTC, no SD card. Everything else that came
up is collected in section 16 (Optional, for later) together with what it would take.

## 1. Goals

1. Remove the DFPlayer Mini and its "stuck until a power cycle" failure for good.
2. Internet radio on the device's own speaker, ten user-editable station slots.
3. Each on-device sound is one replaceable file: chosen from a suggested list on GitHub, or uploaded by the owner,
   always checked before it replaces the current one.
4. Wi-Fi setup over Bluetooth from a phone, the way commercial smart-home devices do it, with the hotspot kept as a
   second method so every phone can set it up (section 8).
5. HTTPS everywhere: prayer data, the audio list and firmware updates come straight from GitHub (no githack
   mirror); OTA updates come from GitHub Releases.
6. Keep every current feature: scheduled athan with the separate Fajr recording and Fajr volume, per-prayer on/off,
   hourly tick and tick window, the tawashih 25 min before Fajr with the relay (today's hidden Q option, renamed
   **Pre-Fajr Tawashih** and made visible: section 6.3), OLED and two-button menu, button lock,
   12 h clock, web page, Home Assistant API, optional display.
7. Keep the per-unit cost about where it is (≈ $6 today for ESP-12F + DFPlayer + SD card).
8. Prayer times stored a whole year at a time: once the clock is set, the device keeps calling the athan without
   internet for as long as it has power (section 9.2).

Non-goals: changing the prayer-time JSON contract (`prayertimes_specs.md`), the mosque list, or anything in the
ESP8266 firmware.

## 2. Why the DFPlayer goes

The DFPlayer is a second microcontroller the ESP cannot see into. The clone chips read the ESP8266 boot-ROM log on
GPIO1 as commands, choke on commands sent back to back, and only recover after a power cycle. The firmware cannot
tell when a track ends (`make_athan` assumes 5 minutes). It also cannot stream: its UART carries only commands, it
has no audio input, and it plays only from its own card.

In version 3 the ESP32-S3 decodes the audio and sends it digitally (I2S) to an amplifier chip. There is no second
processor to lock up, the firmware knows the real playback state, and the same path plays local files and network
streams.

## 3. Hardware

**Decided 2026-10-04: the bare ESP32-S3-WROOM-1-N16R8 module soldered on our own board**, like the ESP-12E today.
It is the cheapest and cleanest option; we design the USB-C and power ourselves, and the front side is SMD-assembled
by the PCB house. Sections 3.1–3.5 describe this build. Section 3.6 lists which ESP32 boards can run the design at
all (useful for prototyping).

### 3.1 Parts

| Part | Role | Price (LCSC qty 1, Oct 2026) |
|---|---|---|
| **ESP32-S3-WROOM-1-N16R8**, LCSC/JLCPCB **C2913202**: 16 MB flash, 8 MB PSRAM, dual-core 240 MHz, 2.4 GHz Wi-Fi + Bluetooth LE, PCB antenna on the module. Not the WROOM-1**U** (external antenna connector) | Wi-Fi, Bluetooth setup, decoding, all device logic | $5.19 (10+: $4.56, 100+: $3.64) |
| **MAX98357AETE+T**, LCSC/JLCPCB **C910544**, TQFN-16 3 × 3 mm | DAC + class-D amplifier in one chip: I2S in, 3.2 W into 4 Ω at 5 V, mono | $1.33 (10+: $1.16, 100+: $0.89) |
| **USB-C receptacle, 16-pin (USB 2.0 data)**, 5.1 kΩ from CC1 and from CC2 to GND, ESD protection on D+/D− (USBLC6-2SC6 or similar) | 5 V power from any charger (C-to-C included) and flashing/logs over the same cable | ≈ $0.30 |
| 3.3 V LDO, ≥ 600 mA (AMS1117-3.3 works from a solid 5 V rail) | ESP rail only; the amp runs from 5 V directly | — |
| Resistors and capacitors | Pull-ups, EN RC, decoupling, bulk | — |
| 2-pin terminal: speaker (4 Ω 3 W, same speaker as today) | Back side | — |
| 2-pin terminal: relay output (GPIO signal + GND, same as today, drives the external IoT relay) | Back side | — |
| 5-way switch (Up, Down, Left, Right, Select), 10 × 10 mm, 6-pin SMD | Back side, hand-soldered (HARDWARE.md 3.5) | — |
| Status LED + resistor | Back side | — |
| 4-pin OLED header (GND, VCC, SCL, SDA) | Back side; the SSD1306 module plugs in or is wired in the enclosure | — |
| Programming pads, **unpopulated**: GND, 3V3, EN, GPIO0, TX0 (GPIO43), RX0 (GPIO44) | Recovery only (section 3.4) | — |

Removed compared with the current board: DFPlayer Mini, microSD card, 3.5 mm AUX jack, the on-board display
footprint (the display becomes a header), the 6-pin power-only USB-C (replaced by the 16-pin data one), the
5-pin programming header, the speaker slide switch SW3 (the amp is shut down from firmware instead), and the
separate 5 V screw-terminal power input (USB-C only; see section 16 if a terminal is still wanted).

**Who makes the analog audio:** the MAX98357A. The ESP32-S3 has no DAC; it only sends digital audio. The DAC and
the power stage are both inside the MAX98357A. Its output drives a speaker directly and is not line level, which
is why a line-out jack would need its own DAC chip (section 16).

**Wi-Fi band:** the ESP32-S3 is 2.4 GHz only, as today. That suits this device (a 128 kbps stream is small,
2.4 GHz goes further through walls, home routers broadcast both bands). 5 GHz would mean the ESP32-C5, which is in
section 16.

### 3.2 Board layout

- **Front (top, SMD, assembled by the PCB house):** ESP32-S3 module, LDO, resistors and capacitors, MAX98357A,
  USB-C receptacle and its ESD part, the unpopulated programming pads.
- **Back (bottom):** the 5-way switch, relay terminal, speaker terminal, status LED, OLED header.
- Use through-hole parts on the back: they are soldered from the front, so the board needs only one-sided SMD
  assembly. SMD parts on both sides cost extra at assembly.
- The module's antenna goes at a board edge, with no copper on either layer under or around it, no back-side part
  under it, and the speaker magnet kept away from it.
- The MAX98357A (3 × 3 mm TQFN with exposed pad) needs reflow, so leave it to the assembly service. Keep the I2S
  traces short and away from the antenna.
- Both the module (C2913202) and the amp (C910544) are "Extended" parts at JLCPCB, which adds a small one-time
  setup fee per part per order. The module is moisture-sensitive (MSL 3), which the assembly house handles.
- Bulk capacitance near the amp: an SMD electrolytic or polymer cap, or several 22–47 µF ceramics, so nothing tall
  sits on the front.
- OLED modules disagree on header order (GND-VCC-SCL-SDA vs VCC-GND-SCL-SDA). Match the modules actually bought
  and print the order on the silkscreen.

### 3.3 Pin map (ESP32-S3-WROOM-1-N16R8; check against the module datasheet when drawing)

| Function | GPIO | Notes |
|---|---|---|
| I2S BCLK / LRCLK / DOUT → MAX98357A | 5 / 6 / 7 | |
| MAX98357A SD_MODE | 15 | Through 4.7 kΩ (changed 2026-10-06 from 560 kΩ; anything 1–47 kΩ): high = left channel only (the firmware sends the same mono samples in both I2S slots), low or floating (boot) = amp off, no idle hiss (`max98357a_amplifier.md` section 5) |
| I2C SDA / SCL → OLED header | 8 / 9 | 4.7 kΩ pull-ups on the board |
| 5-way switch Up / Down / Left / Right / Select | 14 / 10 / 21 / 47 / 11 | Internal pull-ups, common pin to GND (HARDWARE.md 3.5). Down and Select are the old Next and Select pins |
| Relay output | 12 | Series resistor, as today |
| Status LED | 13 | |
| USB D− / D+ | 19 / 20 | Native USB Serial/JTAG |

Do not use: GPIO0, 3, 45, 46 (boot straps; GPIO0 only on the recovery pads), 19/20 (USB), 26–32 (internal flash),
35–37 (octal PSRAM on R8 modules), 43/44 (UART0, recovery pads).

### 3.4 Programming

The USB-C port carries data, so flashing and logs need no programming header: the ESP32-S3's built-in USB
Serial/JTAG enters download mode on its own. The unpopulated pads exist only for recovery: if a bad firmware
crashes before USB comes up, hold GPIO0 to GND while resetting (EN) to force the ROM download mode. They cost
nothing and need no part fitted.

### 3.5 Amplifier and power

- The amplifier circuit copies Adafruit's MAX98357A board (product 3006), with SD_MODE on a GPIO and a gain jumper;
  full schematic, parts (which can be through-hole), gain advice and layout are in **`max98357a_amplifier.md`**.
- Gain: a 2 × 3 jumper header selects 3/6/9/12/15 dB; start at 9 dB (no jumper). At 5 V into 4 Ω the clean
  maximum is 2.5 W at 9, 12 and 15 dB alike; higher gain only clips sooner. Firmware volume is applied on top.
- Speaker wired straight to OUTP/OUTN, no output filter: the chip is filterless, and the speaker wires are about
  5 cm (Maxim recommends the EMI filter only above about 30 cm). The filter is in section 16 for later.
- 5 V 2 A USB-C supply. At full volume into 4 Ω the amp pulls close to 1 A in peaks; the S3 adds about 350 mA
  during Wi-Fi transmit peaks.
- Decoupling: 10 µF + 100 nF at the amp's VDD, ≈ 470 µF equivalent bulk on 5 V near the amp, 22 µF + 100 nF at the
  module's 3V3. EN: 10 kΩ pull-up + 1 µF to GND.

### 3.6 Which ESP32 boards work

The design needs an **ESP32-S3** (two cores and PSRAM for ESPHome's audio stack) with **8 MB PSRAM** (staging
buffer + stream buffers) and **16 MB flash** (two firmware slots + the 9.5 MB audio partition of 4.2).

| Board / module | Flash / PSRAM | Fits? |
|---|---|---|
| ESP32-S3-WROOM-1-N16R8 module (chosen), ESP32-S3-DevKitC-1 N16R8 (prototype, same module) | 16 MB / 8 MB | Yes |
| Seeed XIAO ESP32S3 **Plus** | 16 MB / 8 MB, 20 GPIO (11 edge + 9 rear pads) | Yes, but not chosen (section 14) |
| Seeed XIAO ESP32S3 (regular; for example Amazon B0DJ6NQFKX, 3-pack) | 8 MB / 8 MB, 11 GPIO | Audio only, not storage: after two firmware slots about 4 MB is left, so the slots would have to shrink to ≈ 1.2 MB each (5 min at 32 kbps). Fine for the breadboard prototype with smaller slots |
| ESP32-S3 SuperMini, Waveshare ESP32-S3-Zero and similar | 4 MB / 2 MB | No |
| Classic ESP32 (WROOM-32), ESP32-C3, ESP32-C6 | No or little PSRAM, C3/C6 single core | No |

## 4. On-device audio: one file per sound

### 4.1 What the device keeps

| Slot | Plays | Size limit | Duration limit |
|---|---|---|---|
| `athan` | Dhuhr, Asr, Maghrib, Isha | 3 MB | 5 min |
| `fajr` | Fajr | 3 MB | 5 min |
| `tawashih` | Pre-Fajr Tawashih, 25 min before Fajr (6.3; today a random pick from D1–D10) | 3 MB | 5 min |
| `tick` | Hourly tick | 0.4 MB | 1 min |

MB here means 1,000,000 bytes, the same unit Finder and Windows show, so a user can compare. The menu click and
the volume tone are tiny and never change; they are built into the firmware (`audio_file`).

There is no library, file numbering or choice index on the device. Each slot holds exactly one file plus a short
label (the name from the list, or the uploaded file's name), kept so the web page and the OLED can say which sound is
selected. Changing a sound means replacing that slot's file.

Format: MP3 (any bitrate and sample rate; ESPHome's resampler handles the rest). Mono, or stereo with the whole
sound in the left channel: the clock plays only the left channel (README 1.5). Other formats are
in section 16.

### 4.2 Flash layout

No filesystem. A raw data partition is split into four fixed regions, one per slot. Each region starts with a
4 KB header (magic, length, label, CRC) followed by the file. Playback memory-maps the region (`esp_partition_mmap`)
and hands it to the media player as an in-memory audio file, the same way firmware-embedded files are played. The
file is not copied into RAM.

Proposed partition table for 16 MB (0x1000000):

| Partition | Size | Holds |
|---|---|---|
| bootloader, table, NVS, otadata | 64 KB | as usual (NVS holds the settings and the ten radio URLs) |
| app0 / app1 | 2 × 3.54 MB (0x360000 each) | firmware, two slots for safe OTA |
| prayer | 128 KB (0x20000) | stored prayer-time years: 8 slots of 16 KB (section 9.2) |
| audio | 9.5 MB (0x910000) | athan, fajr, tawashih regions 0x2E0000 each (3,014,656 bytes ≥ 4 KB header + 3 MB), tick region 0x70000 (458,752 ≥ 4 KB + 0.4 MB) |

The four rows fill the 16 MB exactly. Measure the real firmware size on the prototype before fixing the table;
there is roughly 1.5 MB of slack per app slot.

### 4.3 Replacing a file (download from the list, or upload)

1. Receive the whole file into **PSRAM**, never straight into flash.
   - Download: if `Content-Length` is over the slot's limit, refuse before reading. Either way, count bytes while
     reading and abort as soon as the limit is passed.
   - Upload: the same, using the request's length and a running count.
2. Check it in PSRAM: it must parse as MP3 (skip an ID3v2 tag, then find valid frames) and the duration, summed
   from the frame headers (or read from a Xing/Info header), must be within the slot's limit.
3. Any failure (network error, too big, too long, not MP3, the user cancels): free the PSRAM buffer, show the
   reason, and leave the slot untouched. A partial download never reaches flash, so nothing needs cleaning up.
4. Only after every check passes: erase the slot's region, write the file, then write the header last. This takes
   tens of seconds; the OLED and the web page show progress.
5. Refuse to start a download while an athan is playing or another download is running.

Accepted risk: a power cut during step 4 leaves the slot without a valid header. The device then treats the slot
as empty (4.4) and downloads the list's default again at the next boot with internet. Making step 4 atomic costs a
fourth 3 MB region (section 16).

### 4.4 First boot and missing files

- A new device has empty slots. Once Wi-Fi is up, it downloads entry 1 of each list in the catalog (section 5).
- If a slot is empty or invalid at playback time (first boot without internet, or after the power-cut case above),
  the device plays the built-in tone three times and logs it, so the prayer time is still marked audibly.

### 4.5 PSRAM budget (8 MB)

One download at a time: up to 3 MB staging buffer, plus the media player's reader buffers (48 KB per pipeline: ESPHome's 1 MB default starved the I2S speaker and made the router drop the clock at station starts, CLAUDE.md), plus
Bluetooth buffers when setup mode is on (section 8). Local playback is memory-mapped and uses no PSRAM for the file.

## 5. The suggested lists on GitHub

One catalog file in the V3 repository (9.3), for example `docs/audio/catalog.json`, read over HTTPS from
`https://raw.githubusercontent.com/et7ad/esp32_athan/main/docs/audio/catalog.json`:

```json
{
  "athan":    [ { "name": "Reciter 1", "url": "athan/01.mp3" } ],
  "fajr":     [ { "name": "Reciter 1", "url": "fajr/01.mp3" } ],
  "tawashih": [ { "name": "...", "url": "tawashih/01.mp3" } ],
  "tick":     [ { "name": "...", "url": "tick/01.mp3" } ]
}
```

(As built: relative URLs resolve against `docs/audio/`; the radio stations are a separate file, section 7.)

- Up to 10 entries per list. **Entry 1 is the default** (first boot, empty slot). No other indices exist.
- Names: plain ASCII, short enough for the OLED (about 12 characters at the menu font), and every character used
  must be in the font's `glyphs:` list.
- The device fetches the catalog at boot and when a menu or the audio page needs it, and keeps it in RAM only.
- The ESP8266 devices keep reading the daily `docs/athantimes/` files and `docs/timezones/` through githack;
  nothing there moves. Version 3 reads the new yearly files instead (9.2).

### 5.1 The audio library in the V3 repository (decided 2026-10-04)

All current recordings become public in the V3 repository, so any device can preview, stream or download them; owners can
still upload their own (6.1). Layout, committed under `docs/` (today's `SDCard_files/` stays gitignored):

| Folder | From | Entries |
|---|---|---|
| `docs/audio/athan/01.mp3` … `10.mp3` | A1–A10 | regular athans |
| `docs/audio/fajr/01.mp3` … `10.mp3` | F1–F10 | Fajr athans (`fajr/k` is usually the same reciter as `athan/k`) |
| `docs/audio/tawashih/01.mp3` … `10.mp3` | D1–D10 | Pre-Fajr Tawashih |
| `docs/audio/tick/01.mp3` … `10.mp3` | B1–B10 | hourly ticks |
| `docs/audio/catalog.json` | — | names and URLs (names still to be written: reciter names, ASCII) |

C2 (volume tone) and C3 (menu click) are built into the firmware instead. `Z_fallback_*` is no longer needed.

Every file in the library must already pass the device's checks. Measured on 2026-10-04:

| Problem | Files | Fix |
|---|---|---|
| Over 3 MB, length fine | A7, A8, A10, F1, F2, F3, F6, D7, D8, D10 (128–321 kbps) | Re-encode to mono 64 kbps: 5 min = 2.4 MB. The clock plays only the left channel anyway |
| Over 5 min | F8 (5:17, 5.07 MB, no silence to trim) | Owner choice: cut with a short fade-out at 5:00, speed up ≈ 6 %, or use another recording; then mono 64 kbps (5 min at its current 128 kbps would be 4.8 MB) |
| Over 0.4 MB (its 52 s is within the 1-minute limit) | B3 (1.67 MB, 256 kbps) | Re-encode to mono 48 kbps: 0.31 MB. At 64 kbps it would be 0.42 MB, just over; a full-minute tick must stay at or below ≈ 53 kbps |
| Fine as they are | the other 28 files | Leave alone (several are already 16–40 kbps; re-encoding would only lose quality) |

Re-encode recipe (strips tags and cover art, which the device does not need):
`ffmpeg -i in.mp3 -map 0:a -map_metadata -1 -ac 1 -b:a 64k out.mp3` (use `48k` for ticks).
The library then totals about 52 MB (85.6 MB today), well within GitHub's limits for a repo.

## 6. Choosing sounds: web page and device menu

### 6.1 Web: the audio page

ESPHome's select entities have fixed options at compile time, so they cannot show names from the catalog. The
custom component (section 10) therefore serves its own small page, for example `http://athan.local/audio`: plain
HTML forms with no JavaScript, and every check runs on the device. For each slot:

- Which sound is selected (label) and the download status (idle, downloading 40 %, writing, failed: too long, …).
- The ten catalog entries, each with **Preview** (streams the file from the internet for 20 s, nothing is stored)
  and **Download** (the replace flow of 4.3).
- **Upload your own**: file picker + Upload. Same checks; refused before replacing if it is over the size limit or
  over the duration limit.

The main ESPHome page keeps its entities (volume, switches, buttons) and adds text sensors for the four labels, the
download status, and the audio page's address.

### 6.2 Device menu

- **Athan**, **Fajr Athan**, **Tawashih**, **Hourly Tick**: Next steps through the catalog names, streaming a
  preview of each (as today); Select downloads the highlighted one with progress on the OLED; failures show the
  reason and keep the current file. Hourly Tick also has an **Off** entry: a persisted on/off replaces today's
  "None" choice.
- **Pre-Fajr** toggles the Pre-Fajr Tawashih on or off in place, like Clock (6.3). It replaces the Q item.
- New **Radio** item (section 7). Everything else stays: Athan On/Off, Tick Window, Location, Update, Volume,
  Fajr Volume, Clock, Lock Buttons, Info, Cancel.

### 6.3 Pre-Fajr Tawashih (today's hidden Q option, renamed and visible)

The behaviour stays exactly as today; only the name and its visibility change:

- When it is on, 25 minutes before Fajr the device switches the relay output on, plays the `tawashih` slot at the
  Fajr volume, and switches the relay off after 15 minutes. Off by default, as today.
- **Visible everywhere:** a web/Home Assistant switch **Pre-Fajr Tawashih** (template switch,
  `restore_mode: DISABLED`, its `lambda` mirroring the global), the **Pre-Fajr** menu item that toggles in place
  (OLED: `Pre-Fajr` on line 1, `ON` / `OFF` on line 2, since both on one line may not fit 128 px at the menu
  font), and the `tawashih` slot on the audio page (6.1) and in the menu (6.2). The web page's description text
  says what it does, including the relay.
- The ten-Select-press guard (`q_select_count`) goes away; it is a normal toggle now.
- Renames in the V3 yaml: `q_flag` → `prefajr_enabled` (persisted bool), `run_quyam` → `run_prefajr`,
  `quyam_next_hour`/`_minute`/`quyam_triggered` → `prefajr_next_hour`/`_minute`/`prefajr_triggered`. The 25 and
  15 minutes become substitutions (`prefajr_offset_min`, `prefajr_relay_min`) so they live in one place.

## 7. Internet radio

- **Ten slots.** As built (decided 2026-10-05), each slot has a persisted switch **Radio N follows project
  list** (on by default) and a persisted text entity **Radio N own link** (up to 255 characters).
  - A following slot plays station N of `docs/radio/stations.json` in this repository. The list is fetched again
    (as built: at boot and every 6 hours, kept in memory), so the project can move a station and every
    following clock picks it up.
  - A slot that does not follow plays its own link. Empty slots are allowed.
- Web page: the ten switches and link fields, **Play Radio**, **Stop Radio**, and a **Radio Station** select.
  There is no Reset button: switching a slot back to "follows project list" is the reset.
- Device menu: **Radio**. Next moves to the next slot that has something to play (empty ones are skipped). Select
  plays or stops it and returns to the clock. The OLED shows `Radio N` and the station's name from the list (or
  "own link").
- Streams play on the media pipeline; the athan, Pre-Fajr Tawashih, tick and tones use the announcement pipeline.
- **The athan pauses the radio, and the radio resumes when the athan ends** (decided 2026-10-04). The Pre-Fajr
  Tawashih does the same. A live stream cannot stay paused for minutes (the station drops the connection), so
  "resume" means: remember the slot, stop the stream, and start the same slot again after the athan.
- The hourly tick and the menu tones last seconds, so they play over the radio with the radio lowered (mixer
  ducking) instead of pausing it.
- Stop Audio, the buttons in normal mode, and the button-lock silence path stop the radio for good (no resume),
  including when they are pressed during an athan that paused it.
- Supported: direct MP3, Opus or FLAC stream URLs over HTTP or HTTPS (the mp3quran.net stations are 128 kbps MP3).
  Not supported: AAC-only and HLS (`.m3u8`) stations. `.m3u`/`.pls` links must be opened by hand to get the stream
  URL inside.
- If a stream drops (router reboot), retry a few times, then stop and show it on the OLED. Behaviour checked on the
  prototype.

## 8. Wi-Fi setup: Bluetooth first, hotspot as fallback

Two methods, both always built in (decided 2026-10-04). Bluetooth is the main, smoother path. The hotspot (today's
method) stays, so phones that cannot do Bluetooth setup, iPhones in particular, are never stuck.

### 8.1 When setup mode starts

- **New device** (no network saved at all): the hotspot and Bluetooth both start immediately at boot (confirmed in
  the ESPHome 2026.9 source, `WiFiComponent::start()`).
- **After "forget Wi-Fi"** (Select held at power-up; on V2 both buttons): the reset saves a dummy network, so this
  behaves like the next case: Bluetooth after `wifi_timeout` (15 s), the hotspot after `ap_timeout` (about 3 min).
  Either way both methods end up available, and whichever finishes first wins.
- **Saved network unreachable** (new router, changed password, or a router that is simply off): the device keeps
  retrying the saved network. Bluetooth setup starts after `wifi_timeout` (short, for example 15 s) and the hotspot
  after `ap_timeout` (about 3 min, so a brief router reboot does not open a hotspot). When the saved network comes
  back, the device reconnects and both stop.
- The OLED shows which methods are open (line 1 `Wi-Fi setup`, line 2 `Bluetooth`, later `BT + hotspot`; the
  hotspot name goes on the Info screen and in the README, it is too long for one line), and the status LED blinks
  the Improv states.
- Holding any of the four directions at power-up unlocks the buttons.

### 8.2 Method 1: Bluetooth (Improv)

- ESPHome `esp32_improv`, the open Improv standard over Bluetooth LE. The owner picks the device on the phone,
  chooses the network and types the password; the device joins and opens its own page (`next_url`, for example
  `http://{{ip_address}}`; check the supported placeholders at implementation).
- **Physical confirmation:** `authorizer` = the Select button. The OLED shows "Press Select to allow setup", the
  same "press the button on the device" step commercial devices use. It works even when the buttons are locked.
- **Which phones:**
  - Android, and Chrome or Edge on a computer: a setup page on the project's GitHub Pages site with the Improv
    "Connect" button (`improv-wifi-sdk` web component), or improv-wifi.com. No app needed.
  - Any phone with the Home Assistant Companion app: it discovers the device and provisions it.
  - iPhone without Home Assistant: Safari has no Web Bluetooth, so the setup page cannot reach the device. These
    owners use method 2.

### 8.3 Method 2: hotspot (captive portal, as today)

- `wifi: ap:` + `captive_portal:`, unchanged from the ESP8266 firmware: SSID `AthanFallbackHotspot`, password
  `athan404`. The phone joins it, the setup page opens by itself (or at `192.168.4.1`), and the owner picks the
  network and types the password.
- Works on every phone and computer with Wi-Fi.

### 8.4 RAM

ESPHome warns that the Bluetooth stack together with audio components can crash a device. Mitigations:

- `esp32_ble: use_psram: true` moves about 40 kB of Bluetooth buffers out of internal RAM.
- Bluetooth runs only while the device has no Wi-Fi (`ble.enable` 15 s after Wi-Fi went, `ble.disable` on
  connect) and is off in normal use.
- Streaming needs Wi-Fi, so Bluetooth and streaming never run at the same time. What can overlap in setup mode is
  Bluetooth + hotspot + a local athan at prayer time; that combination is on the prototype checklist.

## 9. Online services and prayer data

### 9.1 GitHub over HTTPS

- No githack mirror. The ESP32-S3 does TLS (ESP-IDF certificate bundle), so the device uses raw links on branch
  `main` of the new V3 repository (9.3):
  - `https://raw.githubusercontent.com/et7ad/esp32_athan/main/docs/athantimes/<key>/<year>.json` (yearly prayer
    times with the time zone inside, 9.2)
  - `https://raw.githubusercontent.com/et7ad/esp32_athan/main/docs/audio/catalog.json` and the audio files
- **Firmware updates from GitHub Releases.** Each release carries the OTA binary and a manifest (version, binary
  URL, MD5) as assets. The device reads
  `https://github.com/et7ad/esp32_athan/releases/latest/download/<manifest>`, which redirects to GitHub's file host
  (ESPHome's `http_request` follows redirects). Use ESPHome's `update:` component with the `http_request` platform,
  or keep the current Check/Install flow pointed at the manifest. The device menu and web buttons stay as today.
- **Separate from the ESP8266 by construction:** V3 binaries exist only on the new repository's Releases, and V2
  devices only read the old repository's `docs/firmwareinfo/latest.json` (9.3). That matters because both chips'
  images start with the same 0xE9 byte, so an ESP8266 must never be offered a version 3 binary. Version 3 gets its
  own version numbering.
- GitHub rate-limits unauthenticated raw downloads per IP. A device makes one prayer-times request a year plus the
  occasional catalog, preview or download, far below that; section 16 has a CDN fallback if it ever matters.

### 9.2 Prayer times: one file per year (decided 2026-10-04)

The device downloads a whole year of prayer times for its mosque once and keeps it in flash. After that, the
internet is only needed to set the clock after a power-up; the prayer times never need the network again that year.

**The yearly file** (new, next to the daily folders): `docs/athantimes/<key>/<year>.json`.

```json
{"v":1,"location":"davis","year":2026,"tz":"PST8PDT,M3.2.0,M11.1.0",
 "fields":["fajr","fajr_iqa","sunrise","doha","dhuhar","dhuhar_iqa","asr","asr_iqa","maghrib","maghrib_iqa","isha","isha_iqa"],
 "days":[
["06:06","06:31","07:24","07:44","12:14","12:24","14:39","14:49","16:57","17:12","18:16","18:31"],
...
]}
```

- One row per day; `days[0]` is 1 January; exactly 365 or 366 rows; columns in the order of `fields`.
- Times are true 24-hour `HH:MM` (`14:39`, not `02:39`). The daily files carry afternoon times in 12-hour form and
  today's firmware adds 12 hours on the device. The generator now does that once: Fajr, sunrise and Doha as written;
  Dhuhr +12 if the hour is below 9; Asr, Maghrib and Isha +12 if below 12; each iqama like its prayer.
- `tz` carries the POSIX time zone, so one download covers both. DST changes are computed on the device from that
  string, offline.
- Size: Davis 2026 measured at 36 KB with all 12 time fields; the device refuses anything over 100 KB.
- Generated by a new script (for example `scripts/make_yearly_json.py`) from the existing daily files, so every
  mosque and year already published gets its yearly file at once. The daily files stay in the old repository for
  the V2 devices (9.3).
- Checked on 2026-10-04 against all 11 mosque-years published today: after the conversion every day's adhan times are
  in order (0 problems). 27 days have an iqama published a few minutes before its adhan (for example Davis's fixed
  1:15 Dhuhr iqama on days the adhan is 1:16–1:20); the script warns about those and keeps them as published.
- The script also compares every adhan with the same date of the previous year (through UTC, as the stand-in below
  does) and warns about any difference over a few minutes. Real years differ by at most a minute, so this catches
  typos the ordering check misses. Example: Davis 2025 has Dhuhr at 12:50 and `1:00` (instead of 12:00) on 3 and
  4 December, and wrong Asr and Maghrib on the 4th; those dates are past, and 2026 and 2027 are clean.

#### On the device

- The `prayer` partition (128 KB, 4.2) holds 8 slots of 16 KB. A slot is a header (magic, location key, year,
  day count, time zone, CRC) plus a compact table: every time as minutes since midnight in 2 bytes,
  366 × 12 × 2 = 8.8 KB. A new year goes into a free or unused slot with its header written last, so a power cut
  while writing never damages the year in use.
- Download: into PSRAM (refused over 100 KB), parse, then check that `location` and `year` match what was asked,
  there are 365/366 rows, every value is strict `HH:MM`, and each day's adhan times are in order (Fajr < sunrise <
  Doha < Dhuhr < Asr < Maghrib < Isha). Iqama values are stored but not checked, since nothing is scheduled by them.
  Any failure keeps what is stored and retries later.
- It downloads only when:
  1. the selected mosque's current year is not stored (first boot, or a different mosque chosen);
  2. from 1 December, next year's file. "Not found" is the normal answer for a while: many mosques publish their
     timetable late, sometimes days into January. It is never shown as an error. The device checks once a day in
     December, then every 6 hours from 1 January until the file is stored; after that it is never fetched again;
  3. the web **Refresh Prayer Times** button forces a fresh copy of the current year (for when a mosque corrects
     its timetable).
  Otherwise a stored year is never re-checked.
- Every day at midnight and at boot, the day's row is read from the stored table (local date, day of year) and the
  schedule is computed exactly as today. No network involved.
- The web page and the Info screen show which years are stored (for example `davis 2026, 2027`).

#### Until the new year's file exists, last year's times stand in

From 1 January, while the new year is not stored, the device takes each day's times from the same calendar date
of the previous year (29 February uses 28 February). The athan keeps sounding on time while the mosque has not
published yet.

- **Why that is accurate:** on the published data (Davis, Woodland and masjid15 2026 vs 2027, Davis 2025 vs 2026),
  same-date adhan times differ by at most 1 minute between consecutive years (mean 0.2 min).
- **Daylight saving:** the change dates move each year (US 2026: 8 March and 1 November; 2027: 14 March and
  7 November), and on the dates in between a copied local time is an hour off (seen in the data on 8–13 March and
  1–6 November). So each time is carried over through UTC: last year's local time on that date → UTC with last
  year's offset → local time with this year's offset, both from the stored POSIX time zone. With that, the stand-in
  stays within a minute on every date.
- **No previous year stored either** (for example a brand-new device set up in early January): the device downloads
  the previous year's file once to use as the stand-in.
- **What the owner sees:** a small mark next to the next-prayer time on the OLED (drawn with primitives, like the
  padlock; its exact shape is picked at implementation), and on the web page "Using 2026 times until the 2027
  timetable is published". The README's owner section explains the mark. When the real file arrives, the device
  switches at once and recomputes the next prayer.
- Only with no stored year at all and no internet does the device have no times; the OLED then says so and keeps
  retrying.

#### Time

- After a power-up the clock comes from SNTP, as today. Once set it runs on the module's crystal (drift around a
  second a day) and is re-synced whenever the internet is there, so internet outages after that do not matter.
- After a power cut *with* no internet, the device has no time until the internet returns. The RTC in section 16
  removes that last dependency.

**Owner workflow:** run the script and publish each mosque's next-year files (the yearly file in the V3 repository,
the daily files in the old one, 9.3) as soon as the mosque publishes its timetable, whether that is before December
or some days into January. V3 devices pick it up within a day in December and within 6 hours from 1 January; until
then the stand-in covers it. V2 devices need the daily files by 1 January. As of 2026-10-04, `sclaramca` and
`sclaraalnoor` have data only through 2026; `davis`, `masjid15` and `woodland` already have 2027.

### 9.3 Two repositories (decided 2026-10-04)

Version 3 gets a new GitHub repository (this one); the V2 repository stays for the V2 devices, and both are maintained.

**The new repository** (`et7ad/esp32_athan`, chosen 2026-10-05):

- As built, it started as a fresh folder with only what V3 needs, not as a copy with history. It holds the V3 yaml
  and custom component, `docs/audio/` with the catalog, `docs/radio/`, the yearly prayer files, the scripts, and
  (to come) the V3 KiCad board and enclosure. The V3 firmware binaries go on its GitHub Releases.
- Gets its own README (V3 owners and builders) and its own CLAUDE.md (V3 architecture: no DFPlayer, NVS settings,
  slots, yearly files, HTTPS). This plan and `max98357a_amplifier.md` move there.
- V2-only material (the ESP8266 yaml, the DFPlayer SD-card script and its notes, `docs/firmwareinfo/latest.json`)
  can be removed from the copy, since it lives on in the old repository.

**The V2 repository (`et7ad/esp_athan`)** stays for V2 devices (ESP8266 + DFPlayer) and keeps being maintained:

- Firmware fixes for the V2 yaml, its OTA pointer `docs/firmwareinfo/latest.json`, and new prayer-time years.
- Deployed V2 devices have its githack URLs built in, so these paths must never move or disappear:
  `docs/athantimes/<key>/<year>/<DDD>.json`, `docs/timezones/<key>.json`, `docs/firmwareinfo/latest.json`.
- A note goes at the top of its README, for example: "Building a new clock? Use the version 3 repository:
  &lt;link&gt;. This repository exists to support existing V2 devices (ESP8266 + DFPlayer) and keeps receiving
  their prayer-time updates."

**Prayer data in both repositories:** V2 devices read the daily files from the old repository; V3 devices read the
yearly file from the new one. As built (owner's decision 2026-10-05), the scripts in the V3 repository write **only**
the yearly file, one positional list per day and no per-day files. The V2 daily files are produced in the V2
repository with its own scripts (`break_json.py` and friends). Each new timetable is therefore one run here and
one run there. `scripts/make_yearly_json.py` built the yearly files of every year already published from the V2
daily files, and can do so again for a year that only exists there.

**Mosque list:** each repository's firmware has its own list. A new mosque goes into the V3 list, and also into the
V2 list (with its daily files and time-zone file) if V2 devices there need it.

## 10. Firmware structure

### 10.1 Files

- `firmware/athan.yaml` (as built; the plan called it `athan_v3.yaml`): ESP32-S3, ESP-IDF framework (required by
  ESPHome's audio stack), PSRAM on (octal), 16 MB flash, the partition table of 4.2.
- One custom external component (as built: `firmware/components/athan/`, see `DEVELOPER.md`) for what ESPHome
  lacks:
  1. slot storage: raw regions with headers, read, erase/write, CRC, labels;
  2. downloader: HTTPS into PSRAM with the size limit;
  3. upload handler on ESPHome's web server, plus the `/audio` page;
  4. MP3 validation and duration check;
  5. playback: memory-map a slot, wrap it as an audio file, pass it to the media player's on-device file playback
     (verify that API on the current ESPHome version);
  6. catalog fetch and parse; status for text sensors and the OLED;
  7. yearly prayer times: download, checks, the `prayer` slots, and handing today's row to the yaml (9.2).

  The yaml stays the single place for behaviour (menus, schedule, web entities), as today.

### 10.2 Audio chain

`i2s_audio` → `speaker` (i2s_audio, MAX98357A) → `mixer` speaker (inputs: media, announcement) → `resampler` →
media player with a media pipeline (radio, previews) and an announcement pipeline (athan, tawashih, tick, tones).
MP3 decoder for the slots; MP3/Opus/FLAC for radio. The amp's SD_MODE pin is a GPIO output: on before playback,
off a few seconds after the player goes idle.

### 10.3 Mapping today's scripts

| Today (V2 `firmware/athan.yaml`) | Version 3 |
|---|---|
| `dfplayer:` + `uart:` | Removed |
| Boot: wait 3 s, two `stop`s, volume | Removed |
| `dfp_play` (click/volume tone) | Play the built-in tone on the announcement pipeline |
| `make_athan` (5 min LED timer) | Play slot `athan` or `fajr` (`current_athan_prayer_index == 0`); `athan_playing`, LED and relay follow the real player state |
| `run_quyam` + `q_flag` (random D file, relay 15 min, switched on by 10 Select presses) | `run_prefajr` + `prefajr_enabled`: play slot `tawashih` at the Fajr volume, same relay timing; a normal visible toggle (6.3) |
| hourly tick | Play slot `tick` if tick is on and inside the window |
| `silence_audio` | Stop both pipelines |
| `apply_volume` / `apply_fajr_volume` | Player volume 0.0–1.0; Fajr level for the Fajr athan and the tawashih, then restore, as now |
| `web_preview` (20 s) | Stream the catalog URL until stopped or the next entry, nothing stored |
| `athan_file_index`, `fajr_athan_choice`, `htick_file_index` | Removed (no index); `htick_enabled` (bool) added |
| `dfp_recover`, SD substitutions, `Z_fallback_*` | Removed |
| `wifi: ap:` + `captive_portal` | Kept as the fallback method; `esp32_improv` (Bluetooth) added as the first method (section 8) |
| githack URLs, custom `latest.json` check | raw HTTPS + GitHub Releases (section 9) |
| `load_prayer_times` (daily HTTP fetch of `DDD.json`, +12 PM fix-ups on the device), `refresh_prayer_times_if_needed` | Read today's row from the stored year; downloads only per 9.2; the +12 fix-ups move to the generator script |
| `change_location_handler` (fetches `timezones/<key>.json`) | Downloads the new mosque's yearly file, which carries the time zone |
| persisted `prayer_hours` / `prayer_minutes` / `prayer_times_day` / `prayer_times_year` | Not persisted; the `prayer` partition is the store |
| `web_refresh_times` | Forces a fresh download of the current year (9.2) |

Everything else ports with new pins: schedule and next-prayer logic, menus and `ui_mode` (renumbered
for the new items), web entities and `sync_web_state`, button lock, 12 h rendering, display watchdog. The generic
ESPHome rules still hold: template switches keep `restore_mode: DISABLED`, and nothing touches the display or audio
from a trigger that can fire during setup. Settings live in NVS keyed by id, so the ESP8266's 128-word store and
positional-slot rules in the V2 `CLAUDE.md` do not apply to this yaml. RAM is no longer tight (512 KB SRAM + 8 MB PSRAM);
keeping explicit font `glyphs:` lists is still fine.

## 11. Cost per unit (parts that change)

| | Today | Version 3 |
|---|---|---|
| MCU | ESP-12F ≈ $2 | ESP32-S3-WROOM-1-N16R8 $5.19 (100+: $3.64) |
| Audio | DFPlayer clone ≈ $1–2 + microSD card ≈ $2–3 | MAX98357A $1.33 (100+: $0.89) |
| USB | power-only USB-C | 16-pin USB-C + ESD ≈ $0.30 |
| **Total** | **≈ $6** | **≈ $6.80 each at qty 1, ≈ $4.80 at 100+** (plus JLCPCB's one-time Extended-part fees) |

## 12. Documentation to update when version 3 lands

In the new V3 repository (9.3):

- README, rewritten for V3: the owner section (the two Wi-Fi setup methods, with "iPhone: use the hotspot"
  spelled out; the audio page; radio; upload limits; Pre-Fajr Tawashih; what the OLED's stand-in mark means, 9.2),
  and the data section (yearly files, the generator script, publishing each new year as soon as the mosque does).
- CLAUDE.md, rewritten for V3: new yaml and component, partition table, no DFPlayer, NVS settings, HTTPS URLs,
  update channel, yearly prayer files.
- DEVELOPER.md, plus HARDWARE.md for the V3 board (wiring, BOM, PCB and enclosure instructions in one file).
- `prayertimes_specs.md`: the yearly file (layout, true 24-hour times, `tz`, the 100 KB limit). Done 2026-10-05,
  together with README, CLAUDE.md, DEVELOPER.md and HARDWARE.md.

In the V2 repository: the README note pointing new builders to the V3 repository (9.3). Its daily files keep
coming from its own scripts (section 9.3 as built).

## 13. Prototype and acceptance checklist

Breadboard: **ESP32-S3-DevKitC-1 N16R8** (≈ €6–11; it carries the same ESP32-S3-WROOM-1-N16R8 module as the final
board, so the firmware and partition table carry over unchanged) + a **MAX98357A breakout** (Adafruit 3006, $5.95,
or a clone ≈ $3) + the current speaker, OLED and the 5-way switch. A regular XIAO ESP32S3 (8 MB flash) also works for
everything below except the full 3 MB slot sizes; shrink the slots in its partition table. Its 5V pin is USB VBUS
directly (Seeed schematic v1.2), so the amp breakout can take its 5 V from there.

1. Firmware size measured; partition table of 4.2 confirmed or adjusted.
2. Wi-Fi setup, both methods:
   - Bluetooth from Android Chrome (setup page) and from the HA app; the Select-button confirmation works;
     Bluetooth turns off after joining.
   - The hotspot from an iPhone; it appears after `ap_timeout`.
   - A new device and a device whose saved network vanished both reach setup mode; when the saved network
     returns, the device reconnects by itself.
   - Free internal heap checked with Bluetooth + hotspot on and a local athan playing.
3. Each slot downloads from the catalog; a slot is unchanged after: pulling the network mid-download, a file over
   the size limit (3 MB, tick 0.4 MB), a file over the duration limit (5 min, tick 1 min), a non-MP3 file. The OLED
   and the page show the reason.
4. Upload from the audio page with the same four failure cases.
5. Power cut during the flash write → the slot is treated as empty → the tone plays at prayer time → the default is
   downloaded again at the next boot.
6. Athan, Fajr athan, tawashih and tick play from the memory-mapped slots at 10 %…100 % volume; no hiss when idle
   with the amp shut down. Pre-Fajr Tawashih toggled from the web, from HA and from the menu: it fires 25 min before
   Fajr with the relay on for 15 min.
7. Radio: an mp3quran stream for an hour without dropouts; empty slots skipped; a following slot picks up a link
   changed in `docs/radio/stations.json`; an own link plays; behaviour after a router reboot is known.
8. Radio playing: a scheduled athan pauses it and the same station comes back when the athan ends; the Pre-Fajr
   Tawashih does the same; Stop during the athan leaves the radio off; an hourly tick plays over the lowered radio.
9. Web page, HA API, OLED and the switch respond while a stream plays; heap and PSRAM are stable over hours.
    The 5-way switch: each direction does what its name says as the owner faces the clock (else swap the pins in
    the yaml); every menu row works from Up/Down/Left/Right/Select; quick browsing never restarts the board;
    Left/Right change the volume of a playing athan and radio without stopping them; Up toggles the radio, Down
    the relay; both power-up gestures work; the small previews (Tiny5 pixel font) are readable on the real OLED.
10. OTA from a GitHub Release (redirected asset URL) works; an ESP8266 device does not see it.
11. 5 V current at full volume measured; no brownout resets; no audible Wi-Fi buzz.
12. Prayer times: the generator script builds a yearly file for every published year, and they pass the device's checks.
    After the clock is set, unplugging the router for two days does not stop a single athan. Choosing another mosque
    downloads its year. With the clock set to 1 December the next year is fetched; to 1 January it is used. With
    no next-year file on the server, "not found" shows no error; from 1 January the stand-in runs (marker on the
    OLED, note on the web page) and the athan plays; publishing the file switches the device over within 6 hours.
    Stand-in dates inside a DST window (for example 10 March) come out right, not an hour off. A brand-new device in
    early January with no stored year fetches the previous year for the stand-in. A broken file (wrong year, a
    missing day, times out of order) is refused and the stored year stays. A power cut with no internet shows
    "no time" and recovers by itself when the internet returns.

## 14. Alternatives considered and rejected

| Option | Why not |
|---|---|
| Ready-made ESP32-S3 audio board (Waveshare ESP32-S3-AUDIO-Board ≈ $16) | Fine for prototyping; larger, carries mics/LEDs this device does not need, costs more than the custom board |
| M5Stack Atom VoiceS3R (≈ £14) | Tiny speaker, 8 MB flash, no room for the OLED and buttons |
| Linux board (Pi Zero 2 W, Orange Pi Zero 2W) + I2S amp | Plays everything, but boots in 26–54 s, the SD card can corrupt on power cuts, full rewrite |
| Wi-Fi streamer module (Arylic Up2Stream Mini, $39 without amp) | Closed firmware, phone-app setup, two Wi-Fi devices, ~6× the cost |
| ESP8266 + VS1053 decoder | ESP8266 RAM already exhausted, no practical HTTPS, still needs SD and an amp |
| Raspberry Pi Pico 2 W + BackgroundAudio | No ESPHome audio: full rewrite |
| Other serial MP3 modules (JQ6500, WT2003…) | Same class as the DFPlayer: no streaming |
| ESP32-C3/C6, classic ESP32 | No or limited PSRAM, single core (C3/C6); ESPHome audio targets the S3 with PSRAM |
| XIAO ESP32S3 Plus soldered onto our board instead of the bare module | Simpler board (Seeed did USB-C and power, hand-solderable with an amp breakout), but costs more per unit and needs a stick-on antenna on a U.FL cable; owner chose the bare module (2026-10-04) |
| Filesystem (LittleFS) for the slots | Needs a filesystem component and a file-reading media source; fixed raw regions are simpler and play without copying |

## 15. Decisions (2026-10-04)

No open questions remain. The owner decided:

1. Board: the bare ESP32-S3-WROOM-1-N16R8 module on our own board, SMD-assembled (section 3).
2. Wi-Fi setup: both Bluetooth and the hotspot (section 8).
3. The former Q option is visible everywhere and named **Pre-Fajr Tawashih** (6.3).
4. Hourly tick: at most 0.4 MB and 1 minute (4.1).
5. The athan pauses the radio, which resumes afterwards (section 7).
6. All current recordings go public in the V3 repository under `docs/audio/` for preview, streaming and download; owners can
   also upload their own (5.1).
7. Prayer times come as one file per mosque per year (≤ 100 KB), downloaded once and stored; the internet is then
   only needed for the clock (9.2). Next year's file is looked for from 1 December, may legitimately appear only
   days into January, and last year's times (carried over through the time zone) stand in until it does.
8. Version 3 lives in a new GitHub repository (this one, `et7ad/esp32_athan`); the V2 repository stays and is
   maintained for the V2 devices, with a README note pointing new builders to V3 (9.3).

Still to do: names for the catalog; the F8 choice in 5.1. (Repository name: `et7ad/esp32_athan`, 2026-10-05.)

## 16. Optional, for later

Each item is independent of the others; none is needed for the baseline.

| Idea | What it gives | What it takes |
|---|---|---|
| **5 GHz Wi-Fi: ESP32-C5** (ESP32-C5-WROOM-1-N16R8, dual-band Wi-Fi 6, one band at a time) | Works on 5 GHz-only or crowded 2.4 GHz networks | Single RISC-V core and 384 KB SRAM (S3: two cores, 512 KB). ESPHome supports the C5 on ESP-IDF, but no report found of its audio player on a C5. Run checklist 13 on a C5 dev board; new pin map; still Bluetooth LE for setup |
| **Speaker EMI filter** | Less radio interference if the speaker ever sits on longer wires (Maxim: above about 30 cm), or if the prototype shows Wi-Fi or radio trouble at full volume | A ferrite bead (Murata BLM18SG331TN1D, ≥ 1 A) in series with each output and 680 pF C0G to GND at the terminal; details in `max98357a_amplifier.md` section 8 |
| **RTC** (PCF85063A + CR1220 or supercap) | With the year's prayer times stored (9.2), the clock is the only thing that still needs internet after a power cut; an RTC makes the device fully offline | ≈ $1, on the I2C bus; ESPHome RTC time platform; sync from SNTP, read at boot |
| **3.5 mm line out** | Feed an external speaker or amp | PCM5102A DAC on the second I2S port + jack; a second `speaker` in the chain |
| **Atomic slot replacement** | No empty-slot window if power fails while writing | A fourth 3 MB "spare" region: write the new file into the spare, then swap which region a slot uses (tiny map in NVS). App slots shrink to ≈ 1.9 MB each, so it needs the firmware measured first |
| **Built-in fallback athan** | A real athan, not a tone, when a slot is empty | A short low-bitrate athan in the firmware via `audio_file` (≈ 0.5 MB at 16–24 kbps mono); costs app-slot space twice |
| **Factory audio image** | New devices ship with the default sounds, no first download | Build the four regions + headers on a PC (small script), flash them with esptool at the partition offset together with the firmware |
| **Browser installer** (ESP Web Tools on the GitHub Pages site) | Flash a new device and set Wi-Fi from Chrome over USB, no ESPHome install | `improv_serial` in the yaml + a manifest and page under `docs/` |
| **More upload formats** (Opus, FLAC, WAV) | Owners can upload what they have | Duration parsing per format; decoders already exist in ESPHome |
| **Names for own radio links** | OLED shows a name for a slot that plays its own link (following slots already show the list's name) | One more text entity per slot, or a "name, then URL" field format |
| **AAC / HLS stations** | Stations that are not MP3 | Through Home Assistant's transcoding proxy when HA is used; natively it would need a different audio library (ESP32-audioI2S, Arduino, not ESPHome) |
| **Bigger local library** (microSD) | Many recordings offline | SDMMC socket + `esphome_sd_card` + a custom media source; drops the "one file per slot" simplicity |
| **CDN fallback for GitHub** (jsDelivr) | Keeps working if raw.githubusercontent.com rate-limits or is blocked | Second base URL tried after a failure; jsDelivr serves GitHub repos over HTTPS |
| **5 V screw terminal input** | Hard-wired installations | Terminal + reverse-polarity protection in parallel with USB-C VBUS |
| **Hardware speaker mute switch** | A mute that works without firmware | Slide switch in series with the speaker, as today |
| **Louder audio** | Big rooms, small mosques | TAS5805M-class amp (needs a 12–20 V supply) instead of the MAX98357A |
| **32 MB flash** (ESP32-S3-WROOM-2-N32R8V) | Room for more slots or atomic replacement without shrinking the app | Octal flash; less proven with ESPHome |

## Sources

- ESPHome: [Speaker Audio Media Player](https://esphome.io/components/media_player/speaker/),
  [Speaker Source Media Player](https://esphome.io/components/media_player/speaker_source/),
  [Audio File](https://esphome.io/components/audio_file/),
  [Audio HTTP media source](https://esphome.io/components/media_source/audio_http/),
  [Improv via BLE](https://esphome.io/components/esp32_improv/), [ESP32 BLE](https://esphome.io/components/esp32_ble/)
- Improv on the web: [improv-wifi sdk-js](https://github.com/improv-wifi/sdk-js) (Web Bluetooth: Chrome/Edge on
  desktop and Android, no iOS)
- Module: [ESP32-S3-WROOM-1-N16R8 at LCSC (C2913202)](https://www.lcsc.com/product-detail/C2913202.html),
  [same at JLCPCB](https://jlcpcb.com/partdetail/3198300-ESP32_S3_WROOM_1N16R8/C2913202),
  [ESP32-S3-WROOM-1/1U datasheet v1.8](https://documentation.espressif.com/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf)
- Amplifier: [MAX98357AETE+T at LCSC (C910544)](https://www.lcsc.com/product-detail/C910544.html),
  [same at JLCPCB](https://jlcpcb.com/partdetail/MAX98357AETE+T/C910544)
- Prototype: [ESP32-S3-DevKitC-1 (Espressif docs)](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/),
  [Adafruit MAX98357A breakout (3006)](https://www.adafruit.com/product/3006)
- 5 GHz: [ESP32-C5](https://www.espressif.com/en/products/socs/esp32-c5),
  [ESP32-C5-WROOM-1 datasheet](https://documentation.espressif.com/esp32-c5-wroom-1_wroom-1u_datasheet_en.html),
  [ESP32-C5 vs S3](https://jrattechworks.com/esp32-c5-explained/),
  [ESP32-S31 (2.4 GHz only)](https://www.i-programmer.info/news/91-hardware/18785-new-esp32-s31-super-dual-risc-v-with-wifi-6.html)
- Other: [esphome_sd_card](https://github.com/n-serrette/esphome_sd_card),
  [ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S),
  [Waveshare ESP32-S3-AUDIO-Board](https://docs.waveshare.com/ESP32-S3-AUDIO-Board),
  [mp3quran stream format](https://radio.dubbeh.net/stations/mp3quran-main-2682),
  [Pi Zero 2 boot times](https://community.volumio.com/t/volumio-on-the-raspberry-pi-zero-2/50650?page=4),
  [Arylic Up2Stream Mini](https://arylic.com/products/up2stream-miniv2-receiver-board)
