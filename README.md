# Athan clock, version 3 (ESP32-S3)

A small Wi-Fi clock that calls the athan at the prayer times of your mosque, shows the time and the next prayer on
a little OLED screen, plays Quran radio, and can switch an external relay (for example a mosque speaker) before
Fajr. Version 3 runs on an ESP32-S3 with a MAX98357A amplifier: the audio is stored in the ESP's own flash and
played by the ESP itself.

> **Have an older clock (ESP8266 + DFPlayer, "V2")?** Its firmware and prayer data live in
> [et7ad/esp_athan](https://github.com/et7ad/esp_athan). That repository is kept only to support existing V2
> devices. New builds should use this repository.

**Status (October 2026):** firmware 3.0.0 is written and type-checked but **not yet tested on hardware**. The
prototype checklist is in [version3_planning.md](version3_planning.md) section 13. The board (KiCad) and the
enclosure are still to be drawn from [HARDWARE.md](HARDWARE.md).

## What it does

- Calls the athan at Dhuhr, Asr, Maghrib and Isha, and a separate Fajr athan at Fajr, each at its own volume.
  Each prayer can be switched off on its own.
- Keeps a whole year of your mosque's prayer times in flash. Once the clock is set, the athan keeps working
  without internet.
- Plays internet radio from ten slots. By default they follow this project's station list; any slot can play
  your own link instead.
- **Pre-Fajr Tawashih:** 25 minutes before Fajr it switches the relay on, plays a tawashih, and switches the
  relay off after 15 minutes. Off by default.
- Plays an hourly tick inside a time window you choose, or not at all.
- Every sound (athan, Fajr athan, tawashih, tick) can be swapped for another from the project's list, or for a
  file you upload. Each file is checked before it replaces the old one.
- It is set up from a phone over Bluetooth, or through its own Wi-Fi hotspot.
- It has a web page, works with Home Assistant, and updates its own firmware from GitHub Releases.
- It keeps working without the screen: if the OLED fails, the athan still plays.

## Contents

1. [Using the clock](#1-using-the-clock)
2. [Building one](#2-building-one)
3. [Maintaining the data (project owner)](#3-maintaining-the-data-project-owner)
4. [Repository layout](#4-repository-layout)

---

## 1. Using the clock

### 1.1 First setup: connect it to Wi-Fi

Plug the clock into a USB-C charger (5 V, 2 A). A new clock, or one that cannot reach its saved network, opens
**two setup methods at once**. Use whichever suits your phone; the first one to finish wins.

**Method 1: Bluetooth (Android, computers, Home Assistant app)**

1. On an Android phone or a computer, open **Chrome** or **Edge** and go to <https://www.improv-wifi.com>. Then
   press **Connect** and pick the device named **athan**. Instead, if you use Home Assistant, the Companion app
   finds the clock by itself and offers to set it up.
2. When the screen shows **BT: press Select**, press the **Select** button on the clock. Like commercial smart-home
   devices, this proves you are standing next to it. The permission lasts one minute.
3. Choose your network and type its password. The clock joins and Bluetooth turns off.

**Method 2: hotspot (every phone, including iPhone)**

iPhones cannot use Method 1 without the Home Assistant app, because Safari has no Web Bluetooth.

1. On your phone's Wi-Fi list, join **AthanFallbackHotspot**. The password is **athan404**.
2. The setup page opens by itself. If it doesn't, open <http://192.168.4.1>.
3. Pick your network, type the password and save. The clock restarts and joins.

On a new clock both start right away. When a saved network has gone away, Bluetooth starts after 15 seconds and
the hotspot after about 3 minutes, so a short router restart never opens it.

**Forget the saved Wi-Fi:** unplug the clock, hold **Select** (press the joystick straight in), and plug it back
in. The LED blinks three times, the clock restarts, and it enters setup mode (Bluetooth after 15 seconds, the
hotspot after about 3 minutes).

**Your mosque:** a new clock starts with the first mosque in the list (Davis). Choose yours from the menu
(**Location**) or on the web page. The clock downloads that mosque's year of prayer times. It also downloads
the default sounds the first time it is online, which takes a few minutes.

### 1.2 The screen

Normal screen:

```text
14:05  05Oct        time and date
Asr 16:42 ▫         next prayer and its time
Rem 02:37      ▸ 🔒  time remaining, and status marks
```

| Mark | Meaning |
|---|---|
| Small hollow square after the next prayer's time | **Estimated time.** This year's timetable is not published yet, so the clock uses the same date of last year (converted for daylight saving; at most about a minute off). It disappears when the real timetable arrives. Mosques often publish late, sometimes days into January, so this is normal |
| Last two letters of the prayer's name struck through | The athan for that prayer is switched off |
| Small filled triangle with a number, bottom right | The radio is playing; the number is the radio slot (1–10). **Up** stops it |
| Padlock, bottom right | The buttons are locked (1.3) |
| Circled, crossed-out **W** | No Wi-Fi. The athan keeps working from the stored times |

Other screens:
- **Athan Time: Asr prayer** while the athan plays.
- **A box over the clock for 2 seconds** after Left/Right (the volume and a bar) or Down (Relay ON/OFF).
- **Struck-through rows** (lines through all three rows, with `--:--` where nothing is known) until the clock is set
  after a power-up; lines through rows 2 and 3 only until the day's prayer times are loaded. As on V2.
- **Wi-Fi setup:** while Bluetooth setup or the hotspot is open, it shows `BT: press Select` / `BT: allowed` /
  `BT: joining..` and `Hotspot: on` / `Hotspot: soon`.

### 1.3 Buttons

The clock has one **5-way joystick**: **Up**, **Down**, **Left**, **Right**, and **Select** (press it straight in).

On the clock screen:

| Key | What it does |
|---|---|
| **Left / Right** | Volume down / up by 10 %, shown for 2 seconds. It changes the volume of whatever plays (the Fajr volume during the Fajr athan or the tawashih) and never stops it. With nothing playing, a short tone plays at the new volume. The keys stop at 10 %; 0 % (silent) is only in the menu or on the web page |
| **Up** | Radio on or off. It plays the station chosen last (the number beside the play mark) |
| **Down** | Relay output on or off |
| **Select** | Opens the menu |

While the athan, the tawashih, the tick or a preview plays, **Up**, **Down** and **Select** stop it instead. The
radio is the exception: it keeps playing, so Up stops it, Select opens the menu and Down switches the relay as usual.
Stopping the athan also cancels the radio's automatic resume.

**The menu** is two wheels. **Up/Down** move between the menu's rows; the row's name is at the top, and each row is
live as soon as you reach it: no "open" step. **Left/Right** move through that row's choices. Around the item in the
middle you see faded previews of what is next: the choices to the left and right, the rows above and below with
their current setting. A **check mark** marks what is in use, a **play mark** the radio station playing. Dots under
the item show where you are in the row.

**Select** takes the choice shown. Where nothing needs taking (the choice already in use, a volume, the tick
hours, Info) it closes the menu. Up from the first row or Down from the last also returns to the clock (the faded
preview there says **Home**), and so does 60 seconds without a press.

| Row | Left/Right | Select |
|---|---|---|
| **Radio** | The ten radio slots (opens on the one chosen last) | Plays the slot shown, or stops it if it is playing, and returns to the clock |
| **Athan** | The suggested athans, and **Custom** first if you uploaded one. Rest on one for half a second and its preview plays; the one in use plays from the device. The row opens on the athan in use and previews it (not while the radio plays: then only Left/Right preview, and the radio comes back when the menu closes) | Downloads the one shown and makes it the athan. Progress shows under it; the old sound stays until the new one has passed every check, then the check mark moves. Custom disappears once a download replaces it |
| **Fajr Athan** | The same, for the Fajr athan. Previews play at the Fajr volume | The same |
| **Tawashih** | The same, for the Pre-Fajr Tawashih | The same |
| **Pre-Fajr** | Off, On | Takes it |
| **Hourly Tick** | The ticks, with **Off** first | Takes it; any tick switches the hourly tick on |
| **Tick From**, **Tick Until** | The hour, at once. The line under it shows the window; From = Until means all day | Closes |
| **Athan On/Off** | Fajr, Dhuhr, Asr, Maghrib, Isha, each with On/Off | Switches the prayer shown |
| **Volume**, **Fajr Volume** | 0–100 %, at once, with a tone at the new level | Closes |
| **Location** | The mosques | Takes it; the clock downloads its prayer times |
| **Clock** | 24-hour, 12-hour | Takes it |
| **Update** | — | Checks GitHub for new firmware; when there is one, the item becomes **Install 3.x.y** and Select installs it. `Check failed` means GitHub has no release yet or the clock is offline |
| **Lock Buttons** | — | Locks the buttons and returns to the clock |
| **Info** | IP address, `athan.local`, firmware version | Closes |

**Button lock**, for clocks within children's reach: while locked, any key only stops what is playing. To unlock,
use the **Buttons Locked** switch on the web page, or unplug the clock and plug it back in while holding **any of
the four directions**: the LED blinks three times quickly. (Holding Select instead forgets the Wi-Fi, 1.1.)

### 1.4 The web page

Open **<http://athan.local>**. If that doesn't work, use the IP address from the **Info** screen. The page is
grouped:

- **Now:** Stop Audio, Athan Playing, Next Prayer, Today's Times, Buttons Locked, 12-hour Clock.
- **Athan:** Volume, Fajr Volume, Pre-Fajr Tawashih, Hourly Tick, Tick Window Start / End.
- **Athan On/Off per Prayer:** one switch per prayer.
- **Sounds:** the selected sounds, the last download's status, and the link to the sounds page (1.5).
- **Radio:** Radio Station, Play Radio, Stop Radio, Radio Status.
- **Radio Stations:** the ten slots (1.6).
- **Location and Prayer Times:** Location, Refresh Prayer Times, Prayer Data (which years are stored).
- **System:** Firmware Version (this build, and whether GitHub has a newer one), Firmware (install updates), Check
  For Update, Restart, External Relay, IP address, **Wi-Fi Signal**, **Wi-Fi Drops** (when and why the router dropped
  the clock, the last three), memory.

The same entities show up in Home Assistant when you add the device there (ESPHome integration).

### 1.5 Changing the sounds: <http://athan.local/audio>

Open it from the **Change Sounds At** link on the device page, or type the address. A status bar stays at the top
of the page, with a **Stop** button for previews. Every button reports there, so the page never reloads or jumps
back to the top. After a download or upload, the bar offers **Reload page** to show the new sound. For each of
the four sounds (athan, Fajr athan, tawashih, hourly tick), the page shows:

- **Selected:** the name and length of the sound stored on the clock, the one it plays.
- **The suggested list** from this project, with **Preview** and **Download** for each entry. The selected entry is
  marked *selected*. Preview stores nothing: the selected entry plays from the clock, any other streams from the
  internet. Download fetches the entry, checks it, and only then replaces the selected sound. Downloading the
  selected entry does nothing. To fetch it again, download another entry and then this one.
- **Upload your own:** pick an MP3 file on your phone or computer and press Upload. The uploaded sound then shows
  at the top of the list as **Custom**, with **Preview**, and in the clock's menu as **Custom** too. It stays
  until you download a list entry for that sound, which replaces it.

Limits, checked before anything is replaced:

| Sound | Largest file | Longest |
|---|---|---|
| Athan, Fajr athan, tawashih | 3 MB | 5 minutes |
| Hourly tick | 0.4 MB | 1 minute |

The file must be an MP3. A 5-minute recording fits in 3 MB at 64 kbps mono.

**The clock plays only the left channel.** A mono file always plays in full. A stereo file plays only its left
channel, so the whole sound must be in that channel. A recording with something only on the right (a voice or an
echo) loses it. Converting the file to mono, as below, makes any recording safe. The amplifier is wired for left
only because its mono-mix mode caused audible artifacts during testing.

#### Quick way: an online converter (nothing to install)

1. Open [online-convert.com's MP3 converter](https://audio.online-convert.com/convert-to-mp3) (free, in the
   browser) and choose your file. It also takes WAV, M4A or a video.
2. In the optional settings, set **Change audio channels** to **Mono** and **Change audio sample rate** to
   **44100 Hz**. Set the bitrate to **64 kbps** (48 kbps for an hourly tick).
3. Convert, download the result, and upload it on the clock's `/audio` page. The clock checks every file first
   (MP3, size, length) and says what is wrong if it doesn't fit.

Your file goes to that service's servers. To keep it on your computer, or to cut a long recording to the limit,
use ffmpeg instead.

#### Preparing your own file with ffmpeg

1. Install [ffmpeg](https://ffmpeg.org) once: `brew install ffmpeg` (macOS), `winget install ffmpeg` (Windows) or
   `sudo apt install ffmpeg` (Debian, Ubuntu).
2. See what the file is:

   ```bash
   ffprobe -v error -show_entries stream=channels,sample_rate:format=duration,size -of default=nw=1 in.mp3
   ```

   `channels=1` is mono, `channels=2` is stereo. `duration` is in seconds and `size` in bytes; compare them with
   the limits above.
3. Convert it to mono. This mixes both channels into one, so nothing from either side is lost:

   ```bash
   ffmpeg -i in.mp3 -map 0:a -map_metadata -1 -ac 1 -ar 44100 -b:a 64k out.mp3
   ```

   | Part | What it does |
   |---|---|
   | `-map 0:a -map_metadata -1` | Keeps only the audio, without cover art or tags (the clock does not need them) |
   | `-ac 1` | One channel: mono |
   | `-ar 44100` | 44.1 kHz, the clock's own rate, so it plays without resampling |
   | `-b:a 64k` | 64 kbps: 5 minutes ≈ 2.4 MB. For an hourly tick use `-b:a 48k` |

   The input can be any format ffmpeg reads (WAV, M4A, a video, …). The output is MP3 because the name ends in
   `.mp3`.
4. If the mono version sounds hollow or thin (the two channels cancel each other, which is rare), or one channel
   is only noise, keep a single channel instead of mixing. Replace `-ac 1` with `-af "pan=mono|c0=c0"` for the
   left channel, or `-af "pan=mono|c0=c1"` for the right one.
5. If it is longer than the limit, add `-t 300` (5 minutes) or, for a tick, `-t 60` before `out.mp3` to cut it
   there.
6. Run the `ffprobe` command from step 2 on `out.mp3`: it should say `channels=1` and fit the limits. Then upload
   it.

A failed download, a file that is too big or
too long, or a file that is not MP3 never touches the selected sound. The page says why it refused. A download
is refused while that sound is playing.

### 1.6 Radio

There are ten slots. Each one has two settings on the web page:

- **Radio N follows project list** (on by default): the slot plays station N of this project's list,
  [docs/radio/stations.json](docs/radio/stations.json). The clock keeps the list in memory, loaded at start-up
  and refreshed every 6 hours, so a station starts at once and switching between them is quick. If a station
  moves, the project updates the list and every clock follows within 6 hours (sooner if that station stops
  working on a clock).
- **Radio N own link:** switch "follows project list" off and paste a stream link here (see "Adding your own
  station" below). The screen then shows "own link" for that slot instead of a station name.

Play it from the menu (**Radio**), or with **Radio Station** + **Play Radio** on the web page. The **Radio
Station** list shows each slot's name from the project list ("Radio 1 Cairo Quran"), "(own link)" for a slot
with its own link, and just "Radio N" until the list has loaded. Reload the page to see names that loaded after
it opened.

- **At prayer time** the athan pauses the radio, and the same station comes back when the athan ends. The
  Pre-Fajr Tawashih does the same. If you stop the athan yourself, the radio stays off.
- **The hourly tick** plays over the radio while the radio is turned down for a moment.
- **If Wi-Fi drops** (the router restarts, or drops the clock for a moment), the radio pauses and starts again by
  itself as soon as Wi-Fi is back; after 10 minutes without Wi-Fi it switches off. Pressing Up while Wi-Fi is down
  starts the radio once it is back.
- **If the station itself fails** (down, or no internet behind the router), the clock tries three more times about
  20 seconds apart, then stops and shows the reason under Radio Status.
- **Wrong speed safeguard:** before a station or preview plays for the first time, the clock reads its real format
  from the stream itself and remembers it. Whenever it plays, the clock compares what it is playing with that
  format, all the time, and if they differ it mutes at once and restarts the stream, so Quran never plays sped up
  or slowed down. The first play of each station after a restart takes about half a second longer for this. The
  stored sounds are checked the same way.

#### Adding your own station

A link works when all of these are true:

- **It is the stream itself**, not a web page with a player. Pages on TuneIn, YouTube or a station's own site do
  not work. If the link opens a bare audio player in the browser and starts playing, it is a stream.
- **The format is MP3**, the safest choice, or Opus, FLAC or WAV. The clock reads the format from the server's
  `Content-Type` (`audio/mpeg`, `audio/ogg; codecs=opus`, `audio/flac`, `audio/wav`). If the server sends no type
  it knows, it reads the link's ending (`.mp3`, `.opus`, `.flac`, `.wav`). **Not supported:** AAC or AAC+
  (`audio/aac`, `audio/aacp`, common on Shoutcast stations), Ogg Vorbis, and HLS (`.m3u8`).
- **It is not a playlist file.** A `.m3u` or `.pls` link is a small text file: open it in a text editor and paste
  the `http…` line inside it.
- **`http://` or `https://`.** HTTPS needs a certificate from a common authority; for a site with a self-signed
  certificate, use its `http://` link. An `https://` link that forwards to an `http://` server is refused, so
  paste the `http://` form instead. The project's Cairo station is an example (3.3).
- **It does not expire.** Some services forward to a link with a one-time token (`…?rj-tok=…`) that stops
  working within seconds. Paste the link from before the forward, never the one the browser ends up on.
- **At most 255 characters.**

A stereo station plays only its left channel (1.5). That loses nothing on nearly every station, because both
channels carry the same sound. Any sample rate works; 44.1 kHz plays without resampling. 64 to 128 kbps MP3 is
plenty for one speaker.

**Where to find links:**

- **Quran stations:** [mp3quran.net/eng/radios](https://www.mp3quran.net/eng/radios) lists over 170 stations by
  reciter, all MP3 over HTTPS. The project's own list uses these. The same list as data, with each station's
  `url`: <https://www.mp3quran.net/api/v3/radios?language=eng>.
- **Any station:** [radio-browser.info](https://www.radio-browser.info) is an open directory of tens of thousands
  of stations. Search for one, check that its codec says **MP3**, and copy its stream link.
- **A station's own website:** look for "direct link", "listen in your player" or a `.m3u` / `.pls` download.
  Another way: in Chrome, start the station's web player, open **Developer Tools → Network → Media**, and copy the
  address of the request that keeps loading. Skip it if that address ends in `.m3u8` (HLS).

**Test a link before pasting it** (macOS and Linux have `curl`; on Windows use PowerShell's `curl.exe`):

```bash
curl -sI -X GET --max-time 5 <link>
```

You want `HTTP/… 200` and a `content-type` from the list above. A `302` with `location: https://…` is fine: the
clock follows up to five forwards. A `location: http://…` answer to an `https://` link means: use `http://`. Then
paste it into **Radio N own link**, switch **Radio N follows project list** off, and play the slot.

### 1.7 Prayer times

- The clock stores your mosque's whole year (one download). After that it only needs the internet to set the clock
  after a power cut.
- **Next year** is looked for every day from 1 December. "Not published yet" is normal and is not an error.
- **From 1 January,** if the new timetable is still missing, the clock uses last year's times for the same date
  and shows the hollow-square mark (1.2). It checks every 6 hours and switches the moment the timetable appears.
- **Refresh Prayer Times** (web page) downloads the current year again. Use it if your mosque corrected its
  timetable.
- After a power cut **with no internet**, the clock has no time until the internet comes back, then it carries
  on by itself.

Mosques available now:

| Key (Location) | Mosque | Years |
|---|---|---|
| `davis` | Islamic Center of Davis | 2025–2027 |
| `sclaramca` | MCA Santa Clara | 2025–2026 |
| `sclaraalnoor` | Masjid Al-Noor, Santa Clara | 2025–2026 |
| `woodland` | Woodland Mosque | 2026–2027 |
| `masjid15` | Visalia, California (times from IslamicFinder) | 2026–2027 |

To add your mosque, open an issue or send its yearly timetable. Section 3.1 shows how it is added.

### 1.8 Updates

The clock checks this repository's latest GitHub Release every 6 hours. To install an update, use **Firmware** on
the web page, the **Update** menu item, or Home Assistant. Your settings, sounds and prayer times stay.

**Firmware Version** on the web page always shows the installed version (for example `3.0.0 (latest)` or
`3.0.0 (3.0.1 available)`). ESPHome's own **Firmware** entry shows `UNKNOWN`, with no version, until a check has
succeeded once. That needs a published release (3.4) and internet.

---

## 2. Building one

### 2.1 Hardware

| Part | |
|---|---|
| ESP32-S3-WROOM-1-**N16R8** module (16 MB flash, 8 MB PSRAM), LCSC C2913202 | the board's processor |
| MAX98357A amplifier (MAX98357AETE+T, LCSC C910544) | I2S DAC + 3 W amplifier |
| 4 Ω 3 W speaker, SSD1306 128×64 I2C OLED, a 5-way joystick switch (10 × 10 mm), an LED | |
| USB-C 6-pin (power only), 3.3 V LDO, passives | |
| 1×6 programming header (J6) + a 3.3 V USB-serial adapter (CP2102/CH340) | first flash only |

The full parts list, the pin map, and instructions for the PCB and the enclosure are in
[HARDWARE.md](HARDWARE.md). The amplifier circuit is in [max98357a_amplifier.md](max98357a_amplifier.md).

**Prototype on a breadboard** first: an **ESP32-S3-DevKitC-1 N16R8** (same module), an Adafruit MAX98357A breakout
(3006) or a clone, and the OLED, the 5-way switch and speaker. The wiring is in HARDWARE.md section 2. The firmware and
flash layout are the same as on the final board. Wire the breakout's SD pin to GPIO15 through 4.7 kΩ (left
channel). Left unconnected, SD puts the breakout in its mono-mix mode, which gives audible artifacts.

### 2.2 Firmware

You need [ESPHome](https://esphome.io) **2026.9 or newer**. The firmware was written against 2026.9.1. The
easiest way is **`./build_firmware.sh`** in the repository root (macOS or Linux, Python 3.12–3.14). It installs
that exact ESPHome into its own environment on the first run, with no Docker or dashboard. It keeps ESPHome, the
toolchain (about 1 GB) and the build in `~/.cache/esp32_athan`, so nothing is written into the repository except
the finished binaries in `firmware/binaries/`.

| Command | Does |
|---|---|
| `./build_firmware.sh` | Build. Writes `athan-v3-<version>.factory.bin`, `athan-v3.ota.bin` and `manifest.json` to `firmware/binaries/` |
| `./build_firmware.sh flash [PORT]` | Build and flash over a serial port: the V3 board's J6 adapter (`/dev/cu.usbserial…`) or a DevKitC's USB (`/dev/cu.usbmodem…`). Without PORT it lists the ports |
| `./build_firmware.sh ota [HOST]` | Build and update over Wi-Fi (default `athan.local`) |
| `./build_firmware.sh logs [PORT\|HOST]` | Show the device's log |
| `./build_firmware.sh check` | Validate the yaml only (seconds, no compile) |
| `./build_firmware.sh clean [all]` | Delete the build (`all`: also ESPHome and the toolchain) |

`flash` and `ota` above always build first. When nothing changed that build is quick, but it still takes tens of
seconds. To send binaries that already exist (your last build, or files downloaded from a GitHub Release) without
building, use **`./flash_firmware.sh`**:

| Command | Does |
|---|---|
| `./flash_firmware.sh ota [HOST]` | Sends `firmware/binaries/athan-v3.ota.bin` over Wi-Fi. Keeps settings, Wi-Fi, sounds and prayer times |
| `./flash_firmware.sh usb [PORT]` | Same file over a serial port (J6 adapter or DevKitC USB), firmware only. Keeps everything. For a board that already runs V3 |
| `./flash_firmware.sh usb-full [PORT]` | The whole `athan-v3-<version>.factory.bin` from 0x0: a blank board, or after `partitions.csv` changed. Resets settings and Wi-Fi |

Add `-f FILE` to flash another file. Every file is checked to be an ESP32-S3 image of the right kind first.
Wi-Fi updates (from both scripts) go only to a clock that serves the V3 `/audio` page. A V2 clock also answers to
`athan.local` and an ESP32 image would brick it.

1. **Put the two menu tones** in `firmware/sounds/`: `click.mp3` (the menu click) and `volume.mp3` (the volume
   tone). Any short MP3s work. The V2 SD card's `C3` and `C2` are the original ones. They are built into the
   firmware.
2. **Optional, recommended for your own builds:** generate your own API encryption key (`openssl rand -base64 32`)
   and put it in both `api:` and `ota:` in `firmware/athan.yaml`. You don't need Wi-Fi credentials or
   `secrets.yaml`: Wi-Fi is set up from the phone (1.1).
3. **First flash through J6.** The V3 board's 6-pin USB-C carries power only, so the first flash uses a 3.3 V
   USB-serial adapter on the J6 header (HARDWARE.md 3.2). It also writes the bootloader and the partition table:
   1. Wire the adapter: GND → J6 pin 1, adapter RX → pin 4, adapter TX → pin 5.
   2. Put the jumper cap on J6 pins 1–2 (IO0 to GND), then plug the board's USB-C into a charger.
   3. Flash, choosing the adapter's port:

      ```bash
      ./build_firmware.sh flash        # or, with your own ESPHome: esphome run firmware/athan.yaml
      ```

   4. Remove the jumper, then unplug and replug the USB-C.

   On a DevKitC prototype, just plug in its USB port; it enters download mode by itself. Serial logs come out on
   UART0 (J6 pins 4/5, or the DevKitC's UART port): `./build_firmware.sh logs /dev/cu.usbserial-…`. How much it
   logs is `log_level` at the top of `firmware/athan.yaml`: `DEBUG` while testing, `WARN` (or `NONE`) for a
   finished build.
4. Later flashes can go over Wi-Fi (`./build_firmware.sh ota`), or the clock can update itself from GitHub
   Releases (3.4).

**ESPHome dashboard (Docker) instead of the command line:** copy the *contents* of `firmware/` into the dashboard's
config folder: `athan.yaml`, `partitions.csv`, `web_audio_link.js`, `components/` and `sounds/`. The yaml finds
them by relative path.
Make sure the dashboard image is 2026.9 or newer (`docker pull esphome/esphome:latest`).

**A V2 clock on the same network** is also called `athan`. Give one of them another `device_name` (substitution at
the top of the yaml, for example `athan3`), or `athan.local`, Home Assistant and the dashboard will mix them up.
In a shared dashboard config folder, also save the V3 yaml under another file name.

The first boot with internet downloads the default sounds, which are entry 1 of each list in
[docs/audio/catalog.json](docs/audio/catalog.json). Until then, an athan time is marked with three tones.

Internals are in [DEVELOPER.md](DEVELOPER.md), and the design reasoning is in
[version3_planning.md](version3_planning.md).

---

## 3. Maintaining the data (project owner)

Everything the clocks download lives under `docs/` on branch `main` and is read over HTTPS from
`https://raw.githubusercontent.com/et7ad/esp32_athan/main/docs/...`. A change there reaches every clock without a
firmware update.

### 3.1 Prayer times

One file per mosque per year: `docs/athantimes/<key>/<year>.json`. The format is in
[prayertimes_specs.md](prayertimes_specs.md): one list of 12 `HH:MM` times per day, true 24-hour, at most 100 KB.
All scripts are in `scripts/` and write exactly that one file. They check the times, warn about suspicious days
(adhan out of order, iqama before adhan, a change of more than 3 minutes from the same date last year), and
refuse a broken year.

| Your source | Script |
|---|---|
| A table (spreadsheet exported as a JSON list of rows) | `python3 scripts/source_to_yearly.py --key davis davis_2027.json` |
| IslamicFinder's yearly page (saved HTML) | `python3 scripts/parse_Islamicfinder/parse_islamicfinder_yearly.py --key masjid15 Visalia2027.html` |
| MCA Santa Clara PDF | `python3 scripts/santaclara_prayer_times_parser.py --key sclaramca 2027_MCA_Prayer_Time.pdf` |
| A year that only exists as V2 daily files | `python3 scripts/make_yearly_json.py --key davis --year 2027` (reads `../esp_athan`) |

Add `--dry-run` to check without writing. `python3 scripts/plot_prayer_times.py docs/athantimes` draws every
year as an HTML chart next to its file (needs `pip install plotly`), handy for spotting a typo by eye.

**Publish each new year as soon as the mosque publishes its timetable,** whether that is in November or some days
into January. V3 clocks pick it up within a day in December, or within 6 hours from 1 January. Until then they use
last year's times.

V2 clocks read **daily** files from the V2 repository. Produce those with the V2 repository's own scripts
(`break_json.py` and friends) and commit them there. The scripts here write only the V3 yearly files.

**Adding a mosque:**

1. Add an entry to [docs/athantimes/mosques.json](docs/athantimes/mosques.json): name, POSIX time zone (for
   example `PST8PDT,M3.2.0,M11.1.0`, not `America/Los_Angeles`), and how many minutes after sunrise Doha is.
2. Generate its yearly file(s) with one of the scripts above.
3. Add the key to the `options:` of the **Location** select in `firmware/athan.yaml`, in any position (clocks
   store the key, not the position), and release a firmware update (3.4).
4. Add it to the table in 1.7.

### 3.2 Sounds

`python3 scripts/prepare_audio.py` builds the whole library from the V2 SD-card recordings
(`../esp_athan/SDCard_files`: A → athan, F → fajr, D → tawashih, B → tick). Every file is rebuilt into one plain
format: MP3, mono, 48 kHz (the clock's own rate, so it never resamples), constant 64 kbps, no tags, no cover art,
no info frame. Each gets one plain gain so its decoded peak sits near −2 dBFS (no clipping, similar loudness),
and is checked to be exactly as long as its source; nothing is ever sped up or cut, and a recording over the
length limit is refused. Add `--check` to see what it would do without writing anything.

- The files go in `docs/audio/athan/`, `fajr/`, `tawashih/` and `tick/`, named `01.mp3` … `10.mp3`.
- Names and links go in [docs/audio/catalog.json](docs/audio/catalog.json). Each list holds up to 10 entries.
  Entry 1 is the default that new clocks download. A `url` can be relative to `docs/audio/` (`athan/01.mp3`) or a
  full https link.
- Names are plain ASCII and at most 12 characters, so they fit the screen.
- Every file must already pass the clock's checks (1.5). Re-encode big ones with the `ffmpeg` line in 1.5; use
  `48k` for ticks.
- Clocks re-read the catalog twice a day.

The catalog in this repository is a placeholder ("Athan 1" … "Athan 10") until the recordings are uploaded and
named.

### 3.3 Radio stations

[docs/radio/stations.json](docs/radio/stations.json) has exactly 10 entries; slot *k* of a subscribed clock plays
entry *k*. Change a link there and every subscribed clock uses it the next time that slot plays. An empty `url`
makes that slot empty for subscribers. Names follow the same 12-character rule. Every link must meet the rules
in 1.6 ("Adding your own station"); like the sounds (1.5), a stereo station plays only its left channel. Check
each link with
`curl -sI -X GET --max-time 5 <url>`. A `Location: http://…` answer to an `https://` link means the clock will
refuse it, because ESP-IDF blocks HTTPS-to-HTTP redirects. Use an HTTPS source that answers `200` directly, or
list the `http://` form. Egypt's Quran Radio is an example: its official radiojar link
(`stream.radiojar.com/8s5u5tpdtwzuv`) always redirects to HTTP, so slot 1 uses xecod.com's HTTPS relay. The
radiojar link, written with `http://`, is the fallback if the relay goes away.

### 3.4 Firmware releases

1. Raise `project_version` in `firmware/athan.yaml` (for example `3.0.1`) and run `./build_firmware.sh`. It
   writes `athan-v3.ota.bin` and a matching `manifest.json` into `firmware/binaries/`.
2. If you build another way (dashboard: ⋮ → Download → OTA format), make the two files from the **OTA** image (not
   the factory image) yourself:

   ```bash
   python3 scripts/make_release_manifest.py path/to/firmware.ota.bin --version 3.0.1 --summary "What changed"
   ```

3. Create a GitHub Release with tag `v3.0.1`, attach the two files it wrote (`athan-v3.ota.bin` and
   `manifest.json`), and mark it **latest**. Clocks see it within 6 hours.

A change to `firmware/partitions.csv` cannot be delivered this way. It needs a USB flash on every clock.

---

## 4. Repository layout

```text
firmware/
  athan.yaml               the whole device behaviour (ESPHome)
  partitions.csv           16 MB flash layout (app ×2, prayer years, sounds)
  web_audio_link.js        makes the device page's Change Sounds At value a link (web_server js_include)
  components/athan/        custom component: stored sounds, lists, radio, yearly prayer files, /audio page
  sounds/                  click.mp3 + volume.mp3 (built-in tones; you add them)
  tests/                   host unit tests and a type check (no ESP32 needed)
docs/                      everything the clocks download
  athantimes/              mosques.json + <key>/<year>.json
  audio/                   catalog.json + athan/ fajr/ tawashih/ tick/ (MP3s)
  radio/stations.json      the project's ten radio stations
scripts/                   prayer-time producers, plot, release manifest
hardware/pcb/              KiCad project (to be drawn from HARDWARE.md)
hardware/enclosure/        3D files (to be drawn from HARDWARE.md)
images/                    photos for this README
build_firmware.sh          build / flash / OTA / logs without Docker (2.2)
flash_firmware.sh          flash existing binaries over Wi-Fi or USB, no build (2.2)
README.md  HARDWARE.md  DEVELOPER.md  prayertimes_specs.md  version3_planning.md  max98357a_amplifier.md
CLAUDE.md                  notes for AI coding agents working in this repository
```

## License

See [LICENSE](LICENSE).
