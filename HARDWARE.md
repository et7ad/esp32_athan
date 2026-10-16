# Hardware: the circuit board

The design of the version 3 board: its circuit, pin map, parts, layout and enclosure. The KiCad project is in
`hardware/pcb/`. Building a clock step by step, with the breadboard prototype and the acceptance checklist, is in
[guides/building.md](guides/building.md).

The firmware's pins are the `pin_*` settings at the top of `firmware/athan.yaml`; change both together. Why the
board uses this chip, and which others would work, is in [microcontroller_demand.md](microcontroller_demand.md).
The amplifier circuit is explained in [max98357a_amplifier.md](max98357a_amplifier.md).

1. [Block diagram](#1-block-diagram)
2. [Power](#2-power)
3. [The ESP32-S3 module and pin map](#3-the-esp32-s3-module-and-pin-map)
4. [Amplifier](#4-amplifier)
5. [Relay output and status LED](#5-relay-output-and-status-led)
6. [The 5-way switch](#6-the-5-way-switch)
7. [Parts list](#7-parts-list)
8. [Board layout](#8-board-layout)
9. [Enclosure](#9-enclosure)
10. [Options for later](#10-options-for-later)
11. [Links](#11-links)

## 1. Block diagram

```text
USB-C, power only (J1) ──┐
5 V barrel jack (J2) ────┴── +5V ──┬── MAX98357A (U4) ── speaker terminal J3 (4 Ω 3 W)
                   470 µF bulk (C1)│        ▲ I2S BCLK/LRCLK/DIN, SD_MODE (4.7 kΩ)
                                   │        │
                                   └── AMS1117-3.3 (U3) ── ESP32-S3-WROOM-1-N16R8 (U1)
                                                              ├── DS1 OLED header (I2C) or DS2 TFT header (SPI)
                                                              ├── 5-way switch (U2)
                                                              ├── status LED (D1)
                                                              ├── relay output terminal J5 (3.3 V signal + GND)
                                                              └── UART0, EN, IO0 ── J6 programming header
```

## 2. Power

- **J1, USB-C receptacle,** 6-pin, power only, soldered on the front or the back (footprint
  `USB_C_Receptacle_GCT_USB4125_6P_BothSides` in the project library: the same pads on both sides, the four shell
  legs in shared holes; section 8):
  - VBUS (A9, B9) to **+5V**.
  - CC1 (A5) and CC2 (B5) each through 4.7 kΩ to GND (R1, R2). USB-C asks for 5.1 kΩ ±20 %, so anything from 4.08
    to 6.12 kΩ works; without them a C-to-C charger gives no power.
  - GND (A12, B12) and the shield to GND.
  - On the back the receptacle lies flipped, so its A and B pins trade places: CC1 and CC2 swap (each has its own
    resistor), VBUS and GND stay on their nets.
  - It has no data lines: the first flash and the serial log go through J6 (section 3).
- **J2, barrel jack** (CUI PJ-102AH), centre positive, also on **+5V**. It takes a regulated 5 V supply only. Use one
  input at a time: the two are joined directly.
- **C1, 470 µF electrolytic** (8 × 10.5 mm) on +5V: the bulk capacitor for the amplifier's peaks. It is the one tall
  part, on the front.
- **C2, 22 µF** on +5V at the regulator's input.
- **U3, AMS1117-3.3** (SOT-223, LCSC C6186) makes **+3V3** for the module and the display. The amplifier runs from
  +5V.
- **C3 22 µF and C4 100 nF** on +3V3, right at the module's 3V3 pin. C3 is also the regulator's output capacitor.

## 3. The ESP32-S3 module and pin map

**U1 is an ESP32-S3-WROOM-1-N16R8** (LCSC C2913202). Use exactly this module: the variants without PSRAM, or with
less flash, cannot hold the stored sounds.

- GND pins and the exposed pad to GND, with thermal vias under the pad.
- **EN:** 4.7 kΩ to +3V3 (R3) and 1 µF to GND (C5). EN must not float, and the capacitor holds the module in reset
  until the 3.3 V supply is steady.
- **Boot pins:**

  | Pin | On the board | Why |
  |---|---|---|
  | GPIO0 | 4.7 kΩ to +3V3 (R4) | High at reset is a normal boot; low at reset (the J6 jumper) is download mode |
  | GPIO45 | nothing; never pull it high | Its internal pull-down selects the 3.3 V flash supply. High selects 1.8 V and the module won't start |
  | GPIO46 | nothing | Internal pull-down; must be low for download mode |
  | GPIO3 | nothing | Selects the JTAG source, only after an eFuse is burned |

- **J6, programming header,** 1×6, 2.54 mm. GND sits next to IO0, so a jumper cap on pins 1 and 2 at power-up
  selects download mode:

  | J6 pin | 1 | 2 | 3 | 4 | 5 | 6 |
  |---|---|---|---|---|---|---|
  | Signal | GND | IO0 (GPIO0) | EN | TXD0 (GPIO43) | RXD0 (GPIO44) | +3V3 |
  | USB-serial adapter | GND | jumper to pin 1 | | adapter RX | adapter TX | not connected |

  Use a 3.3 V adapter (CP2102, or a CH340 set to 3.3 V), and power the board from its own supply while flashing:
  the adapter's 3.3 V pin cannot supply the module's Wi-Fi current. An ESP-Prog works too and needs no jumper (it
  drives EN and IO0 itself, through J6 pins 3 and 2).
- **Not connected:** GPIO19 and GPIO20 (USB, unused with a power-only USB-C), GPIO3, GPIO45, GPIO46 (boot pins),
  GPIO26 to GPIO32 (flash), and GPIO33 to GPIO37 (the PSRAM on R8 modules).

| Signal | GPIO | Goes to |
|---|---|---|
| I2S_BCLK | 5 | MAX98357A BCLK (pin 16) |
| I2S_LRCLK | 6 | MAX98357A LRCLK (pin 14) |
| I2S_DOUT | 7 | MAX98357A DIN (pin 1) |
| AMP_SD | 15 | 4.7 kΩ (R5) to MAX98357A SD_MODE (pin 4) |
| DISP_SDA | 8 | DS1 SDA (OLED, I2C data) and DS2 SDA (TFT, SPI data); 4.7 kΩ to +3V3 (R8) |
| DISP_SCL | 9 | DS1 SCK (OLED, I2C clock) and DS2 SCL (TFT, SPI clock); 4.7 kΩ to +3V3 (R9) |
| DISP_RES | 16 | DS2 RES (TFT reset) |
| DISP_DC | 17 | DS2 DC (TFT data or command) |
| DISP_CS | 18 | DS2 CS (TFT chip select) |
| DISP_BL | 4 | JP4 to DS2 BLK (TFT backlight, dimmed by PWM) |
| BTN_UP | 14 | 5-way switch, Up |
| BTN_DOWN | 10 | 5-way switch, Down |
| BTN_LEFT | 21 | 5-way switch, Left |
| BTN_RIGHT | 47 | 5-way switch, Right |
| BTN_SELECT | 11 | 5-way switch, centre push |
| RELAY | 12 | JP1 to relay terminal J5 pin 2 |
| LED | 13 | 100 Ω (R10) to the status LED D1 |
| TXD0 / RXD0 | 43 / 44 | J6 pins 4 and 5 (UART0: flashing and the log) |

Free GPIOs: 1, 2, 38, 39, 40, 41, 42 and 48.

**The display headers.** The board takes one display, on one of two headers; the firmware is built for one or the
other (guides/building.md section 3).

| Pin | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| DS1, OLED, 4 pins (SH1106 1.3" or SSD1306 0.96", I2C) | GND | +3V3 | SCK | SDA | | | | |
| DS2, TFT, 8 pins (ST7789 1.54", 240×240 IPS, SPI) | GND | +3V3 | SCL | SDA | RES | DC | CS | BLK |

- The TFT modules call their SPI clock and data SCL and SDA. Both headers share those two lines, and the I2C
  pull-ups stay: SPI drives the lines push-pull, and each pull-up only costs about 0.7 mA while its line is low.
- OLED modules differ in pin order (GND or VCC first). Check yours against the pin names printed at the header
  before soldering.
- **JP4** (a solder jumper, bridged) links the TFT's backlight pin to GPIO4, which dims it. On most modules BLK
  drives a small transistor and is pulled up on the module, so cutting JP4 leaves the backlight on at full
  brightness. A module that wires BLK straight to its LEDs needs a transistor instead.
- Both displays run from +3V3: an OLED draws about 20 mA, a TFT 20 to 40 mA (mostly its backlight).
- Choose a TFT with an **IPS** panel: a clock is read from across a room, often from an angle, where a TN panel
  washes out. The firmware drives the ST7789. Small differences between ST7789 modules (colour order, inversion,
  offset) are settings at the top of `firmware/displays/tft.yaml`.

## 4. Amplifier

**U4 is a MAX98357AETE+T** (TQFN-16, LCSC C910544), wired as in
[max98357a_amplifier.md](max98357a_amplifier.md):

- The exposed pad to GND, with thermal vias.
- VDD (pins 7 and 8) from +5V, with C6 22 µF and C7 100 nF right at the pins.
- SD_MODE through R5 (4.7 kΩ) from GPIO15: high plays the left channel only, low shuts the amplifier down.
- OUTP and OUTN straight to the speaker terminal J3, with no output filter.

**Gain.** Solder jumpers set it. JP3 has three pads: its centre is the amplifier's GAIN pin, one side goes to
+5V, and the other side goes to GND through JP2 (bridged), or through R7 (100 kΩ) once JP2 is cut.

| Gain | Jumpers |
|---|---|
| 9 dB (as the board comes) | JP3 open |
| 6 dB | JP3: centre bridged to the +5V side |
| 12 dB | JP3: centre bridged to the JP2 side, JP2 bridged |
| 15 dB | JP3: centre bridged to the JP2 side, JP2 cut, R7 (100 kΩ) fitted |

Start at 9 dB. If the loudest athan distorts at full volume, lower the gain; if it is too quiet, raise it.

## 5. Relay output and status LED

- **J5, relay terminal:** pin 2 is GPIO12 through JP1 (bridged), pin 1 is GND. It is a 3.3 V logic signal for a
  relay module, not a relay itself. To put a resistor in series, cut JP1 and fit R11 (100 Ω).
- **D1, status LED** (blue, 1206), from GPIO13 through R10 (100 Ω, about 5 mA). R10 suits the LED's colour: 100 Ω
  for blue, green and white (about 2.7 to 3 V across the LED), 300 to 330 Ω for red, orange and yellow (about 2 V).
  Either way it stays well under the LED's 20 mA and what GPIO13 can drive.

## 6. The 5-way switch

**U2 is a 10 × 10 mm through-hole 5-way switch** (four directions and a centre push, each closing to one common
pin): the Alps SKQUCAA010 or one of its many copies, sold as "5-way switch 10*10*10 mm". Alps' SKQUAAA010 looks the
same and fits the same holes but has no centre push, so the menu could not be opened with it. Every SKQU model
shares the footprint in `hardware/pcb/libs/SKQUAAA010/`.

| Pin | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|
| Function | Up (A) | Left (B) | Down (C) | COM | Right (D) | Centre (push) |
| Goes to | GPIO14 | GPIO21 | GPIO10 | GND | GPIO47 | GPIO11 |

- No resistors: the firmware turns on the internal pull-ups (about 45 kΩ) and filters contact bounce (40 ms).
- The five lines are identical inputs, so any assignment works. If one direction does the wrong thing, change the
  `pin_up`, `pin_down`, `pin_left`, `pin_right` and `pin_select` settings at the top of `firmware/athan.yaml`
  rather than the board.
- None of these GPIOs is a boot, flash, PSRAM, USB or JTAG pin. GPIO47 runs at the flash voltage, which is 3.3 V on
  the N16R8.
- "Up" is the direction you push as you face the clock. The switch sits on the front, the side you face, so the
  board's front view in KiCad shows the directions as you use them.

## 7. Parts list

| Ref | Part | Side |
|---|---|---|
| U1 | ESP32-S3-WROOM-1-N16R8 (LCSC C2913202) | back |
| U2 | 5-way switch with a centre push, 10 × 10 mm through-hole (Alps SKQUCAA010 or a copy; not SKQUAAA010) | front |
| U3 | AMS1117-3.3, SOT-223 (LCSC C6186) | back |
| U4 | MAX98357AETE+T, TQFN-16 (LCSC C910544) | back |
| J1 | USB-C receptacle, 6-pin, power only (GCT USB4125-GF-A, LCSC C3151650) | front or back |
| J2 | Barrel jack, CUI PJ-102AH (LCSC C3096093) | front |
| J3, J5 | 2-pin screw terminals, 5.0 mm pitch (MaiXu MX126-5.0-02P, LCSC C5188434), speaker and relay | front |
| J6 | 1×6 pin header, 2.54 mm, with a jumper cap | front |
| DS1 or DS2 | 1×4 header for the OLED, or 1×8 header for the TFT (only the one you use) | front |
| C1 | 470 µF 10 V low-ESR electrolytic, 8 × 10.5 mm (Panasonic EEE-FK1A471P, LCSC C178530) | front |
| C2, C3, C6 | 22 µF 25 V ceramic, 1206, X5R or X7R (Samsung CL31A226KAHNNNE, LCSC C12891) | back |
| C4, C7 | 100 nF 50 V X7R, 1206 (Samsung CL31B104KBCNNNC, LCSC C24497) | back |
| C5 | 1 µF 50 V X7R, 1206 (Samsung CL31B105KBHNNNE, LCSC C1848) | back |
| R1 to R5, R8, R9 | 4.7 kΩ 1 %, 1206 (UNI-ROYAL 1206W4F4701T5E, LCSC C17936) | R1, R2 front; the rest back |
| R10 | 100 Ω 1 %, 1206 (UNI-ROYAL 1206W4F1000T5E, LCSC C17901); 300 to 330 Ω with a red LED | back |
| R7 | 100 kΩ 1 %, 1206 (UNI-ROYAL 1206W4F1003T5E, LCSC C17900); only for 15 dB gain, not fitted otherwise | back |
| R11 | 100 Ω 1 %, 1206 (UNI-ROYAL 1206W4F1000T5E, LCSC C17901); only for a series resistor on the relay output, not fitted otherwise | back |
| D1 | Blue LED, 1206 (NationStar NCD1206B1, LCSC C130717) | front |
| JP1, JP2, JP4 | solder jumpers, bridged as made; JP3 open | back |
| H1 to H4 | M3 mounting holes | |
| | Display module: 1.3" SH1106 (or 0.96" SSD1306) 128×64 I2C OLED, or 1.54" ST7789 240×240 IPS TFT | wired to its header |
| | Speaker, 4 Ω 3 W | wired to J3 |

Every small part is 1206, a size that is easy to solder by hand. The LCSC numbers are also in the schematic (the
LCSC field), for JLCPCB to assemble the board: [guides/jlcpcb_assembly.md](guides/jlcpcb_assembly.md).

Prices (LCSC, October 2026): the module costs $5.19 (10+: $4.56, 100+: $3.64), the MAX98357A $1.33 (10+: $1.16,
100+: $0.89).

## 8. Board layout

The board is 59.6 × 50 mm, with two layers. Every part is soldered by hand, on both sides.

- **Front** (the side you look at): the TFT header DS2 at the top centre and the OLED header DS1 above the centre
  (only one display is fitted, on long headers, so low parts can sit under it), the 5-way switch at the OLED's right
  edge, the USB-C and barrel jacks on the left, the speaker and relay terminals on the right, the status LED at the
  top left (always visible), and the tall capacitor C1, never under a display.
- **Back:** the module with its antenna end at the bottom edge, over a copper keep-out on both layers, and the
  amplifier, the regulator and the small parts above it.
- **USB-C:** J1 has its pads on both sides and its four shell legs in shared holes. Solder it on the front by hand,
  or have JLCPCB place it on the back ([guides/jlcpcb_assembly.md](guides/jlcpcb_assembly.md)). Its opening is at
  the left edge either way; on the back it sits about 5 mm lower, below the board.
- **Antenna:** no copper, metal screws or speaker magnet near the antenna end. The mounting holes beside it take
  nylon screws.
- **Amplifier:** short I2S traces kept away from the antenna; solid ground under the amplifier, with thermal vias in
  its exposed pad; its capacitors right at VDD; wide traces to the speaker terminal.
- **Power and ground:** wide +5V traces (1 mm or more; the amplifier draws peaks of about 1 A), a continuous ground
  pour, and stitching vias around the board and the amplifier.
- **Silkscreen:** each resistor's and capacitor's value next to it; J6's name and pins on both sides; the pin
  names at both display headers; the switch's directions (UP, DN, LT, RT, SEL); the gain settings (6 dB, 12 dB,
  "GAIN open = 9 dB"); SPK+, SPK− and RLY+ at the terminals; "5V DC, centre +" at the barrel jack; and "Athan V3",
  the project's address and a QR code on each side.

Before ordering: run KiCad's DRC and ERC, compare the module's and the amplifier's footprints with their
datasheets, and check the 3D view for anything tall where it should not be.

## 9. Enclosure

The enclosure is not designed yet. The version 2 enclosure (`../esp_athan/hardware/encolsure/Iteration13/`, with
round and rectangular speaker variants) is a good starting point. It needs:

| Item | What it needs |
|---|---|
| Board mount | The board's outline and its four M3 holes |
| Power | Openings for the USB-C (big enough for common cable boots, about 12 × 7 mm; about 5 mm lower, below the board, when J1 is on the back) and the barrel jack |
| J6 | Reachable with the case open (for the first flash), or a small slot to flash it closed |
| Switch | An opening or a cap over the 5-way switch's stem, with room to tilt it four ways and press it in |
| LED | A light pipe or a hole over it |
| Display | A window and clips for the module, and room for its wires to the header (4 for the OLED, 8 for the TFT). Modules differ in size and in where their visible area sits, so take the window from the seller's drawing |
| Speaker | A sealed space behind it sounds fuller; grille holes in front of the cone |
| Antenna | No metal, screws or speaker magnet within about 15 mm of the module's antenna end |
| Relay terminal | A cable exit for the two relay wires |
| Ventilation | A few slots; the amplifier and the module get warm at full volume |
| Labels | Arrows around the switch, and optionally a QR code to this repository |

Put the exported STL, STEP or F3D files in `hardware/enclosure/`.

## 10. Options for later

None of these is needed for the clock, and each is independent of the others. Other chips (5 GHz Wi-Fi with an
ESP32-C5, 32 MB of flash) are in microcontroller_demand.md section 3; firmware ideas are in DEVELOPER.md section 12.

| Idea | What it gives | What it takes |
|---|---|---|
| **A real-time clock** (PCF85063A with a CR1220 or a supercap) | With the year's prayer times stored, the time is the only thing that still needs the internet after a power cut. An RTC makes the clock fully offline | About $1 on I2C (with a TFT the I2C pins carry SPI, so two of the free GPIOs); ESPHome's RTC time platform, synced from SNTP and read at boot |
| **USB data on the USB-C** | Flashing and the log over the charging cable, without J6 | A 16-pin USB 2.0 receptacle with D+/D− on GPIO20/19 and a USBLC6-2SC6 ESD part; a few cents more, and harder to solder by hand |
| **3.5 mm line out** | Feeds an external speaker or amplifier | A PCM5102A DAC on the second I2S port and a jack; a second speaker in the firmware's audio chain. The MAX98357A's output is a speaker output, not line level |
| **Speaker EMI filter** | Less radio interference if the speaker ever sits on wires longer than about 30 cm | A ferrite bead and a 680 pF C0G capacitor on each output: max98357a_amplifier.md section 8 |
| **Hardware speaker mute switch** | A mute that works without the firmware | A slide switch in series with the speaker |
| **Louder audio** | Big rooms, small mosques | A TAS5805M-class amplifier (it needs a 12 to 20 V supply) instead of the MAX98357A |
| **A bigger local library** (microSD) | Many recordings offline | An SDMMC socket, `esphome_sd_card` and a custom media source; it gives up the one-file-per-sound design |

## 11. Links

- Module: [ESP32-S3-WROOM-1/1U datasheet](https://documentation.espressif.com/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf),
  [C2913202 at LCSC](https://www.lcsc.com/product-detail/C2913202.html),
  [at JLCPCB](https://jlcpcb.com/partdetail/3198300-ESP32_S3_WROOM_1N16R8/C2913202)
- Amplifier: [C910544 at LCSC](https://www.lcsc.com/product-detail/C910544.html),
  [at JLCPCB](https://jlcpcb.com/partdetail/MAX98357AETE+T/C910544); the circuit is in
  [max98357a_amplifier.md](max98357a_amplifier.md)
- Prototype: [ESP32-S3-DevKitC-1](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/),
  [Adafruit MAX98357A breakout (3006)](https://www.adafruit.com/product/3006)
