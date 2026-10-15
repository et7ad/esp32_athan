# Microcontroller demand

Version 3 runs on an **ESP32-S3-WROOM-1-N16R8** (16 MB flash, 8 MB octal PSRAM, LCSC C2913202). This file explains
which feature needs which part of that chip, so that a future build can drop features and use a smaller or cheaper
one. Section 5 lists the other designs that were considered. Prices are in [HARDWARE.md](HARDWARE.md) section 4,
the wiring in the rest of that file.

Figures are from the firmware as of October 2026 (3.0.0); sizes are binary (1 MB = 1024 KB). The firmware itself is
about **1.8 MB** (the last build's OTA image: 1.78 MB).

## 1. What the chip has to provide

| Resource | What this design needs | What drives it |
|---|---|---|
| **CPU** | ESP32-S3, two cores at 240 MHz | Decoding radio streams and stored MP3s, resampling and mixing, next to Wi-Fi, HTTPS and the main loop |
| **Flash** | 16 MB | The four stored sounds (9 MB), two firmware slots for updates over Wi-Fi (2 × 3.4 MB) |
| **PSRAM** | 8 MB, octal | Holding a downloaded sound while it is checked (up to 3 MB); HTTPS buffers for the radio and downloads; the audio pipeline's buffers and task stacks; Bluetooth buffers |
| **Internal RAM** | the S3's 512 KB | Wi-Fi (its buffers stay in internal RAM on purpose: [firmware_rules.md](firmware_rules.md) section 4), Bluetooth, the worker task, everything ESPHome keeps there |
| **Radio** | 2.4 GHz Wi-Fi and Bluetooth LE | Everything online; Bluetooth only for the Wi-Fi setup |
| **Peripherals** | I2S (the amplifier), I2C (the OLED) or SPI (a TFT) | |
| **GPIO** | 13 | I2S: 3 (GPIO5, 6, 7); amplifier on/off: 1 (GPIO15); OLED: 2 (GPIO8, 9; a TFT build: 6, also GPIO16, 17, 18, 4); 5-way switch: 5 (GPIO14, 10, 21, 47, 11); relay: 1 (GPIO12); LED: 1 (GPIO13) |

The flash layout (`firmware/partitions.csv`), 16 MB in total:

| Partition | Size | Used for |
|---|---|---|
| `nvs`, `otadata` | 28 KB | Settings, which firmware slot boots |
| `app0`, `app1` | 3.4 MB each | Two firmware slots: an update writes the other one, so a failed update never leaves the clock without firmware. The firmware is 1.8 MB; the rest is room to grow |
| `prayer` | 128 KB | Up to 8 years of prayer times (16 KB each) |
| `audio` | 9.1 MB | Athan, Fajr athan and tawashih (3 MB and 5 min each), the hourly tick (448 KB, 1 min) |

## 2. Feature by feature

What each feature costs, and what dropping it would free.

| Feature | Flash | PSRAM / RAM | CPU | Dropping it frees |
|---|---|---|---|---|
| **Stored sounds**: the athan, Fajr athan, tawashih and tick play from the clock's own flash | 9.1 MB (`audio`) | None for playing: the file is read straight from flash (memory-mapped) | MP3 decoding | 9 MB of flash. 8 MB of flash is then enough. But the athan would need the internet at prayer time (streamed), or another store (an SD card, a separate player as on V2) |
| **Downloading and uploading sounds**: the suggested lists, the `/audio` page's upload | (the slots above) | The whole file, up to 3 MB, in PSRAM while it is checked (it must be a valid MP3 of the right length before it touches flash), plus one HTTPS connection | Light | The 3 MB peak. With smaller sound slots the peak shrinks with them |
| **Internet radio**, streamed previews, the Random tawashih | None | HTTPS: about 40 KB per connection (two can be open: a stream and a download). The media pipeline's 48 KB reader buffer, the decoders, and the task stacks of the player, mixer and resamplers (all in PSRAM) | Decoding MP3, Opus, FLAC or WAV in real time, beside Wi-Fi and HTTPS | The main reason for an S3 with PSRAM. Without it, PSRAM is needed only for the download staging |
| **Prayer times**: a year per download over HTTPS, kept for 8 years | 128 KB (`prayer`) | The download (at most 100 KB) and the parsed tables in PSRAM | Light | Little. Any ESP32 can do this part |
| **Updates over Wi-Fi** (GitHub Releases, the web page) | A second firmware slot (3.4 MB) | One HTTPS download | Light | 3.4 MB of flash. Without it, firmware goes on over USB only |
| **Bluetooth Wi-Fi setup** (Improv) | Some firmware size | About 40 KB of Bluetooth buffers (moved to PSRAM: `esp32_ble: use_psram`) | Light | That RAM. The hotspot setup still works. Needs a chip with Bluetooth LE (not the ESP32-S2) |
| **Web page, `/audio` page, Home Assistant API** | Firmware size | Connections in internal RAM; the `/audio` page is built in PSRAM | Light | Some RAM and firmware size |
| **OLED and menu** | Fonts (small) | The 1 KB screen buffer and the menu rows in PSRAM (a TFT: a 115 KB frame, also in PSRAM) | One draw is about 23 ms on the I2C bus (a whole TFT frame about 50 ms: 30 to send it over SPI, 20 to draw; the TFT clock draws it every 30 s and on changes, and only its seconds, about 2.5 ms, every other second) | Almost nothing |
| **5-way switch, relay, LED, hourly tick, time zone, schedule** | Small | Small | Light | GPIO only |

In short:
- **16 MB of flash** is for the **stored sounds** (9 MB) and the **two firmware slots** (2 × 3.4 MB).
- **8 MB of PSRAM** is for the **radio and other streams** (HTTPS, decoding, buffers) and for **checking a downloaded
  sound before it is stored** (up to 3 MB at once).
- The **ESP32-S3** (two cores) is for decoding audio in real time beside Wi-Fi and HTTPS.

## 3. Which chips can do what

| Chip or board | Flash / PSRAM | Works with this firmware? |
|---|---|---|
| **ESP32-S3-WROOM-1-N16R8** (chosen), ESP32-S3-DevKitC-1 N16R8 (the prototype, same module) | 16 MB / 8 MB | Everything |
| Seeed XIAO ESP32S3 **Plus** | 16 MB / 8 MB | Everything (20 GPIO, enough for the 13 needed). Not chosen for the PCB: it costs more per clock and needs a stick-on antenna on a U.FL cable; Seeed's board would have spared us the USB-C and power parts |
| ESP32-S3 with 8 MB flash and 8 MB PSRAM (WROOM-1-N8R8, the regular Seeed XIAO ESP32S3) | 8 MB / 8 MB | Everything, with smaller sound slots: after two firmware slots about 4 MB is left, so about 1.2 MB per sound (5 min at 32 kbps). Needs the changes in section 4 |
| ESP32-S3 with 2 MB PSRAM (WROOM-1-N4R2, N8R2, SuperMini, Waveshare ESP32-S3-Zero) | 4–8 MB / 2 MB | Not as is. The radio would fit, but a 3 MB sound cannot be held for checking, and 4 MB of flash leaves no room for stored sounds next to two firmware slots |
| ESP32-S3 without PSRAM (**WROOM-1-N4, WROOM-1-N8**) | 4–8 MB / none | No radio or streams: HTTPS, the decoders and their buffers do not fit beside Wi-Fi and Bluetooth in the 512 KB of internal RAM. No sound downloads. A clock with prayer times, the menu, the web page and short stored sounds might fit after a rework; untested |
| Classic ESP32 with PSRAM (WROVER modules) | 4–16 MB / 4–8 MB (quad) | Possible in principle (two cores, Bluetooth), but only 4 MB of its PSRAM is directly usable, it is slower, and its pins differ. A port, untested |
| ESP32-S2 | 4 MB / none or 2 MB | No Bluetooth (hotspot setup only) and a single core for audio, Wi-Fi and HTTPS. Not recommended |
| ESP32-C3, ESP32-C6 | 4–8 MB / none | Single core, no PSRAM: no radio or stored-sound downloads. Only a basic clock with an external player |
| ESP32-S3-WROOM-2-N32R8V | 32 MB / 8 MB | Everything, with room for more or bigger sound slots or atomic sound replacement (DEVELOPER.md section 12). Octal flash, less proven with ESPHome; untested |
| ESP32-C5 (C5-WROOM-1-N16R8: dual-band Wi-Fi 6, one band at a time) | 16 MB / 8 MB | Untested. It would work on 5 GHz-only or crowded 2.4 GHz networks, but it has one RISC-V core and 384 KB of internal RAM (the S3: two cores, 512 KB). ESPHome supports the C5 on ESP-IDF; no report found of its audio player on one (October 2026). A new pin map; still Bluetooth LE for the setup |
| ESP8266 + DFPlayer (V2, [et7ad/esp_athan](https://github.com/et7ad/esp_athan)) | 4 MB / none, sounds on the player's SD card | V2's design: the DFPlayer plays sounds from its SD card. No radio, no downloads |

The cheaper modules save little: the N16R8 is $5.19 at quantity 1, $4.56 for 10+ (LCSC, October 2026).

## 4. Dropping features: what the firmware would need

| Build | Board it allows | What changes in the firmware |
|---|---|---|
| Everything (today) | 16 MB / 8 MB | Nothing |
| Smaller stored sounds (for example 1.2 MB, about 5 min at 32 kbps) | 8 MB / 8 MB (N8R8, XIAO ESP32S3) | `firmware/partitions.csv`: smaller `audio` partition; `components/athan/audio_slots.cpp` (`REGION_OFFSET`, `REGION_SIZE`) and `audio_slots.h` (`SLOT_MAX_BYTES`, `SLOT_MAX_MS`); the suggested sounds in `docs/audio/` re-encoded to fit; `esp32: flash_size`. A changed partition table needs a USB flash on every clock |
| No stored sounds (the athan streamed at prayer time) | 8 MB / 8 MB | As above, the `audio` partition removed, and the athan scripts playing a stream. The athan then fails whenever the internet is down at prayer time, which is what stored sounds are for |
| No updates over Wi-Fi | One firmware slot: about 3.4 MB of flash back | `partitions.csv` (one `factory` app slot); the `update:` and OTA blocks in `athan.yaml`. Every update then needs USB |
| No Bluetooth setup | Saves RAM and some firmware size | Remove `esp32_ble` and `esp32_improv` from `athan.yaml`; the hotspot stays. Needed for an ESP32-S2 |
| No radio and no streams (stored sounds only) | Still needs PSRAM for the 3 MB download check, so still an S3 with PSRAM unless the sounds are small | Remove the radio entities, scripts and station list (`athan.yaml`, `athan.cpp`), the Random tawashih and streamed previews |
| No radio, no stored sounds | Almost any ESP32, with an external player | V2's design: a different firmware, not a trimmed V3 |

For any board without octal PSRAM, `athan.yaml` also needs its `psram:` block, `task_stack_in_psram`,
`esp32_ble: use_psram` and `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC` adjusted, and the GPIO substitutions at the top of
the file checked against the new board (HARDWARE.md 3.2 lists the pins to avoid).

## 5. Other designs considered

V3 drops V2's DFPlayer because it is a second microcontroller the ESP cannot see into: the clone chips read the
ESP8266's boot log as commands, choke on commands sent back to back and recover only after a power cycle; the
firmware cannot tell when a sound ends; and it plays only from its own SD card, so it cannot stream. On the S3 the
ESP decodes the audio itself and sends it digitally (I2S) to the amplifier, so there is no second chip to lock up,
the firmware knows what is playing, and the same path plays stored sounds and streams.

| Option | Why not |
|---|---|
| A ready-made ESP32-S3 audio board (Waveshare ESP32-S3-AUDIO-Board, about $16) | Fine for a prototype; bigger, carries microphones and LEDs the clock does not need, and costs more than our board |
| M5Stack Atom VoiceS3R (about £14) | A tiny speaker, 8 MB flash, no room for the screen and the switch |
| A Linux board (Pi Zero 2 W, Orange Pi Zero 2W) + an I2S amplifier | Plays everything, but boots in 26–54 s, its SD card can corrupt on a power cut, and it would be a full rewrite |
| A Wi-Fi streamer module (Arylic Up2Stream Mini, $39 without an amplifier) | Closed firmware, set up through a phone app, two Wi-Fi devices, about 6 times the cost |
| ESP8266 + a VS1053 decoder | The ESP8266's RAM is already full and it has no practical HTTPS; still needs an SD card and an amplifier |
| Raspberry Pi Pico 2 W + BackgroundAudio | No ESPHome audio: a full rewrite |
| Other serial MP3 modules (JQ6500, WT2003, …) | The same kind of part as the DFPlayer: no streaming |
