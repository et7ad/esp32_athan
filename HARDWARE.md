# Hardware: board, enclosure, wiring

Instructions for drawing the version 3 PCB in KiCad (`hardware/pcb/`) and the enclosure (`hardware/enclosure/`),
and for wiring a breadboard prototype first, with the acceptance checklist for both (section 9) and options for
later (section 10). Why this chip, and which others would work, is in `microcontroller_demand.md`. The amplifier
circuit is in `max98357a_amplifier.md`. The firmware pin assignments are the `pin_*` substitutions at the top of
`firmware/athan.yaml`, so change both together.

Status: the board is drawn (`hardware/pcb/`, README 2.1); the enclosure is not yet. Build the prototype (section 2)
and run the checklist in section 9 before ordering boards.

## 1. Block diagram

```text
USB-C 6-pin (5 V only) ──┬── 5 V rail ──┬── MAX98357A (VDD) ── speaker terminal (OUTP/OUTN, 4 Ω 3 W)
   CC1/CC2 5.1 kΩ        │   bulk cap   │        ▲ I2S BCLK/LRCLK/DIN, SD_MODE (4.7 kΩ)
                         │              │        │
                         └── LDO 3.3 V ─┴── ESP32-S3-WROOM-1-N16R8 ── I2C ── OLED header (SSD1306)
                                                 ├── 5-way switch: Up/Down/Left/Right/Select (to GND)
                                                 ├── status LED
                                                 ├── relay output terminal (logic signal + GND)
                                                 └── UART0 + EN + IO0 ── J6 programming header
```

## 2. Prototype on a breadboard (do this first)

Parts: **ESP32-S3-DevKitC-1 N16R8**, an **Adafruit MAX98357A breakout (3006)** or a clone, the speaker, a
128×64 SSD1306 I2C OLED, the 5-way switch (3.5; five push buttons work too), an LED + 1 kΩ, and a 4.7 kΩ
resistor for the amp's SD pin.

| From (DevKitC pin) | To | Notes |
|---|---|---|
| 5V | breakout **Vin** | DevKitC's 5V pin carries USB VBUS when powered over USB |
| GND | breakout **GND**, OLED GND, switch **COM**, LED cathode | common ground |
| GPIO5 | breakout **BCLK** | |
| GPIO6 | breakout **LRC** | |
| GPIO7 | breakout **DIN** | |
| GPIO15 | breakout **SD**: see the note below | |
| breakout **GAIN** | open (9 dB) | other gains: `max98357a_amplifier.md` section 4 |
| breakout **+ / −** | speaker | |
| 3V3 | OLED VCC | |
| GPIO8 / GPIO9 | OLED SDA / SCL | most OLED modules already carry 4.7–10 kΩ pull-ups |
| GPIO14 | switch **Up** | internal pull-up; the switch's COM goes to GND |
| GPIO10 | switch **Down** | internal pull-up |
| GPIO21 | switch **Left** | internal pull-up |
| GPIO47 | switch **Right** | internal pull-up |
| GPIO11 | switch **Select** (centre push) | internal pull-up |
| GPIO12 | 1 kΩ → relay input (or an LED to see it) | |
| GPIO13 | 1 kΩ → LED → GND | status LED |

**SD on the breakout:** wire GPIO15 → 4.7 kΩ → SD, as on the PCB. The breakout's own 1 MΩ from SD to Vin can
stay: with GPIO15 low the 4.7 kΩ pulls SD to about 0.02 V (shutdown), with GPIO15 high to about 3.1 V (left
channel). Don't leave SD unconnected: the breakout then sits in its mono-mix mode, which gave audible artifacts in
testing, and it never shuts down.

Flash it over either DevKitC port and watch the log on its **UART** port: the firmware logs on UART0, which on the
final board is the J6 header. Then run the checklist in section 9. The firmware, partition table and pins
are the same as on the final board.

## 3. Schematic (KiCad)

Draw it as four blocks. Values come from the Espressif module datasheet (v1.8, "peripheral schematics", linked in
section 11) and the Adafruit 3006 board. Net names in **bold** match the firmware.

### 3.1 Power and USB

- **USB-C receptacle, 6-pin, power only** (owner's choice 2026-10-05; KiCad symbol
  `Connector:USB_C_Receptacle_PowerOnly_6P`). Use V2's footprint
  `Connector_USB:USB_C_Receptacle_GCT_USB4125-xx-x_6P_TopMnt_Horizontal` if your connectors fit it, otherwise draw
  one from the seller's drawing:
  - VBUS (A9, B9) → **+5V** (through the polyfuse).
  - CC1 (A5) and CC2 (B5) → 5.1 kΩ each to GND. Without these, a C-to-C charger gives no power (the V2 board left
    them open).
  - GND (A12, B12) and the shield → GND.
  - No D+/D−: the ESP32-S3's own USB is not reachable, so the first flash and serial logs go through J6 (3.2).
    A 16-pin USB 2.0 receptacle (D+/D− to GPIO20/19 with a USBLC6-2SC6 ESD part) would bring back flashing and
    logs over the charging cable; it is a few cents more and needs the assembly service to solder.
- **Optional polyfuse** 1.5–2 A in VBUS (a footprint costs nothing).
- **5 V bulk** near the amplifier: about 470 µF in total. Either one SMD aluminium/polymer cap (6.3 V or more), or
  several 22–47 µF X5R ceramics (count with DC-bias derating). Keep everything on the front low; nothing tall.
- **3.3 V regulator,** at least 600 mA, for the module only. The amp runs from +5V.
  - AMS1117-3.3, SOT-223, LCSC C6186: 10 µF in. Its 22 µF output cap is the module's 22 µF below, so place the
    regulator next to the module.
  - Or a low-dropout part such as AP2112K-3.3 (600 mA) or AP7361C-33 (1 A). That leaves more margin when VBUS
    sags under amplifier peaks.
- **+3V3** at the module: 22 µF + 100 nF right at pin 2 (C3, C4 in the drawing).

### 3.2 ESP32-S3-WROOM-1-N16R8 (LCSC C2913202)

Use exactly this module: the N4 and N8 have no PSRAM, and smaller flash cannot hold the stored sounds.
[microcontroller_demand.md](microcontroller_demand.md) explains which feature needs what, and which boards work
without which features.

- GND pins and the exposed pad → GND (thermal vias under the pad).
- **Strapping pins, compared with the ESP8266:** the ESP-12E needed GPIO0 and GPIO2 pulled high, GPIO15 low, and
  RST/CH_PD high. The ESP32-S3 module has internal pulls on its boot pins that already select a normal boot, so the
  only part needed is on EN:

  | Pin | Needs | Why |
  |---|---|---|
  | EN | 10 kΩ to +3V3 + 1 µF to GND | must not float; the RC delays start-up until the 3.3 V rail is stable |
  | GPIO0 | nothing (optional 10 kΩ pull-up, DNP) | internal pull-up = normal boot; low during reset = download mode |
  | GPIO45 | nothing, **never pull high** | internal pull-down = 3.3 V flash supply; high = 1.8 V and the module won't boot |
  | GPIO46 | nothing | internal pull-down; must be low for download mode |
  | GPIO3 | nothing | JTAG source select, only with an eFuse burned |
  | RST, GPIO2, GPIO15 | nothing | not straps on the S3 (EN is the reset; GPIO15 drives the amp's SD here) |

  No DTR/RTS auto-reset transistors: one jumper cap on J6 selects download mode instead.
- **EN:** 10 kΩ to +3V3 + 1 µF to GND (power-on delay), and to J6.
- **J6 programming header,** 1×6 2.54 mm, populated (a jumper cap stays with the board). GND sits next to IO0, so
  a jumper on pins 1–2 at power-up selects download mode:

  | J6 pin | 1 | 2 | 3 | 4 | 5 | 6 |
  |---|---|---|---|---|---|---|
  | Net | GND | IO0 (GPIO0) | EN | TXD0 (GPIO43) | RXD0 (GPIO44) | +3V3 |
  | USB-serial adapter | GND | jumper to pin 1 | — | adapter RX | adapter TX | leave open |

  Use a 3.3 V adapter (CP2102, or a CH340 set to 3.3 V). Power the board from its USB-C while flashing; the
  adapter's 3.3 V pin cannot supply the module's Wi-Fi current peaks. An ESP-Prog works too and needs no jumper
  (it drives EN and IO0 itself, wired to J6 pins 3 and 2).
- **USB_D− (GPIO19), USB_D+ (GPIO20):** not connected (power-only USB-C).
- Leave unconnected: GPIO3, GPIO45, GPIO46 (straps), GPIO26–32 (flash), GPIO33–37 (octal PSRAM on R8 modules).

| Signal | GPIO | Goes to |
|---|---|---|
| **I2S_BCLK** | 5 | MAX98357A BCLK (pin 16) |
| **I2S_LRCLK** | 6 | MAX98357A LRCLK (pin 14) |
| **I2S_DOUT** | 7 | MAX98357A DIN (pin 1) |
| **AMP_SD** | 15 | 4.7 kΩ → MAX98357A SD_MODE (pin 4), left channel |
| **DISP_SDA** | 8 | J4 SDA (OLED, I2C) and J7 SDA (TFT, SPI data), 4.7 kΩ to +3V3 |
| **DISP_SCL** | 9 | J4 SCL (OLED, I2C) and J7 SCL (TFT, SPI clock), 4.7 kΩ to +3V3 |
| **DISP_RES** | 16 | J7 RES (TFT reset) |
| **DISP_DC** | 17 | J7 DC (TFT data / command) |
| **DISP_CS** | 18 | J7 CS (TFT chip select) |
| **DISP_BL** | 4 | JP4 → J7 BLK (TFT backlight, PWM) |
| **BTN_UP** | 14 | 5-way switch Up (3.5) |
| **BTN_DOWN** | 10 | 5-way switch Down |
| **BTN_LEFT** | 21 | 5-way switch Left |
| **BTN_RIGHT** | 47 | 5-way switch Right |
| **BTN_SELECT** | 11 | 5-way switch centre push |
| **RELAY_OUT** | 12 | 1 kΩ → relay terminal pin 1 (pin 2 = GND) |
| **LED** | 13 | 1 kΩ → LED → GND |
| **TXD0 / RXD0** | 43 / 44 | J6 pins 4 / 5 (UART0: flashing and logs) |

Free GPIO left: 1, 2, 38, 39, 40, 41, 42, 48.

**The display headers.** The board takes one display at a time, on one of two headers side by side; the firmware is
built for one or the other (`--display tft`, README 2.2):

| Pin | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| **J4** OLED, 1×4 (SSD1306 0.96" or SH1106 1.3", I2C) | GND | +3V3 | SCL | SDA | | | | |
| **J7** TFT, 1×8 (1.54" ST7789, 240×240 IPS, SPI) | GND | +3V3 | SCL | SDA | RES | DC | CS | BLK |

- The TFT modules call their SPI clock and data SCL and SDA. Both headers share those two nets, so the I2C
  pull-ups stay: SPI drives the lines push-pull, and each pull-up costs about 0.7 mA while its line is low.
- **JP4** (solder jumper, bridged) links the backlight pin to GPIO4, which dims it by PWM. On most modules BLK drives a
  small transistor and is pulled up on the module: cut JP4 and the backlight stays on at full brightness. A module
  that wires BLK straight to its LEDs needs a transistor instead.
- Both run from +3V3: an OLED draws about 20 mA, a TFT 20–40 mA (mostly its backlight).
- Buy the TFT with an **IPS** panel: a clock is read from across a room, often from an angle, where a TN panel such
  as the 1.8" ST7735's washes out. The firmware drives the ST7789 only (`mipi_spi`, model ST7789V). Panel
  differences between ST7789 modules (colour order, inversion, offset) are settings at the top of
  `firmware/displays/tft.yaml`.

### 3.3 Amplifier

Copy `max98357a_amplifier.md` section 3 exactly:

- MAX98357AETE+T (LCSC C910544), TQFN-16 with exposed pad to GND.
- 10 µF + 100 nF at VDD.
- SD_MODE through 4.7 kΩ from GPIO15: high = left channel only, low = shutdown.
- 2×3 gain jumper header (no jumper = 9 dB). As `Conn_02x03_Odd_Even`: pins 3 and 4 = GAIN, 2 = +5V (2–4: 6 dB),
  6 = GND (4–6: 12 dB), 1 = 100 kΩ to +5V (1–3: 3 dB), 5 = 100 kΩ to GND (3–5: 15 dB).
- OUTP/OUTN straight to the speaker terminal, no output filter.

Add footprints for the optional EMI filter (section 8 there) only if you want the option later, populated with
0 Ω / DNP.

### 3.4 Back-side parts (through-hole, soldered from the front)

The same footprints as the V2 board (`../esp_athan/hardware/pcb/athan_kicad`):

| Part | Footprint (V2) |
|---|---|
| 5-way switch (SMD, hand-soldered: 3.5) | drawn from the seller's drawing |
| Speaker terminal, relay terminal | `TerminalBlock:TerminalBlock_MaiXu_MX126-5.0-02P_1x02_P5.00mm` |
| Display headers J4 (OLED, 1×4) and J7 (TFT, 1×8), 3.2 | 2.54 mm pin headers. **Print the order on the silkscreen**; OLED modules differ (GND-VCC or VCC-GND first) |
| Status LED | 3 mm or 5 mm THT LED |
| Mounting holes | `MountingHole_3.2mm_M3` × 4 |

Removed compared with V2: DFPlayer, microSD, 3.5 mm jack, the on-board display footprint, speaker slide switch,
5 V screw-terminal input, the two push buttons (now one 5-way switch, 3.5). Kept: the 6-pin power-only USB-C (now
with CC resistors) and a programming header (J6, 1×6, new pin order).

### 3.5 The 5-way switch

The common 10 × 10 mm SMD 5-way switch with 6 pins (sold as Bestol, uxcell 10×10×9, TS-1505 type): four directions
and a centre push, each closing to one common pin. Only one key closes at a time.

- **Wiring:** COM → GND; each direction and the centre → its GPIO. No resistors: the firmware turns on the internal
  pull-ups (about 45 kΩ) and debounces (40 ms).
- **Pins:** GPIO10 and GPIO11 (the old Next and Select) plus **GPIO14, GPIO21, GPIO47**. All five sit on the module's
  bottom edge (module pins 18, 19, 22, 23, 24; GPIO12/13, relay and LED, are pins 20/21 between them), and none
  is a strapping, flash/PSRAM, USB or JTAG pin. GPIO47 runs at the flash voltage, 3.3 V on the N16R8 (it would be
  1.8 V only on the 1.8 V "V" modules). On a DevKitC-1, GPIO14 is on the left header next to GPIO13, and GPIO21
  and GPIO47 are on the right header.
- **Any assignment works.** The five lines are identical inputs, so route whatever is shortest and change the
  `pin_up` / `pin_down` / `pin_left` / `pin_right` / `pin_select` substitutions at the top of `firmware/athan.yaml`
  to match.
- **Pinout (community-reported for this switch type; sellers differ, so check yours):**

  | Pad | 1 | 2 | 3 | 4 | 5 | 6 |
  |---|---|---|---|---|---|---|
  | Function | **COM** (→ GND) | Left | Centre (Select) | Up | Right | Down |

  Check it with a meter in continuity mode before laying out: one probe on the pad you think is COM, then tilt
  the stem each way and press it in; each of the other five pads should beep for exactly one of them. If two pads
  beep for everything, the probe is not on COM.
- **Orientation:** "Up" is the direction the owner pushes, as they face the clock. The switch sits on the back of
  the board (3.4), so the side the owner sees is the mirror of the board's front view in KiCad: left and right
  swap. Name the nets by the owner's view, or swap `pin_left` / `pin_right` in the yaml after the first test.
- **Assembly:** the switch is SMD but sits on the back, where the board has only THT parts (one-sided assembly,
  section 5). Solder it by hand after assembly (its pads are large), or pay for assembly on both sides.
- **Footprint:** KiCad has none for this switch; draw it from the seller's drawing. The Ultra Librarian library in
  `hardware/pcb/libs/ul_SKQUCAA010/` is for the **Alps SKQUCAA010**, a different part: through-hole snap-in, its
  pins numbered 1 Up (A), 2 Left (B), 3 Down (C), 4 COM, 5 Right (D), 6 Centre (Alps datasheet), and listed by
  Alps as not recommended for new designs. Its footprint and numbering do not fit the SMD switch.

## 4. Bill of materials (per board)

| Qty | Part | LCSC | Side |
|---|---|---|---|
| 1 | ESP32-S3-WROOM-1-N16R8 (not the -1U) | C2913202 | front, SMD |
| 1 | MAX98357AETE+T | C910544 | front, SMD |
| 1 | USB-C receptacle, 6-pin power only (owner's part) | — | front |
| 1 | AMS1117-3.3 (or AP2112K-3.3 / AP7361C-33) | C6186 for AMS1117 | front |
| 2 | 5.1 kΩ (CC) | Basic part | front |
| 2 | 10 kΩ (EN; GPIO0 pull-up optional, DNP) | Basic part | front |
| 2 | 4.7 kΩ (I2C pull-ups) | Basic part | front |
| 2 | 100 kΩ (gain header: 3 dB and 15 dB positions) | Basic part | front |
| 2 | 1 kΩ (LED, relay) | Basic part | front |
| 1 | 4.7 kΩ (SD_MODE, left channel; anything 1–47 kΩ) | Basic part | front |
| 1 | 1 µF (EN) | Basic | front |
| 2 | 100 nF (3V3, amp VDD) | Basic | front |
| 1 | 22 µF (3V3 at the module, also the LDO output cap) | Basic | front |
| 2 | 10 µF (LDO in, amp VDD) | Basic | front |
| — | ≈ 470 µF bulk on 5 V (one SMD cap or several ceramics) | — | front |
| 1 | 2×3 pin header + 1 jumper (gain) | — | front or back |
| 1 | 5-way switch, 10 × 10 mm, 6-pin SMD (TS-1505 type, 3.5) | — | back, hand-soldered |
| 2 | MX126 2-pin 5.0 mm terminals | — | back, THT |
| 1 | 1×4 pin header (J4, OLED) | — | back, THT |
| 1 | 1×8 pin header (J7, TFT), or leave it off on an OLED clock | — | back, THT |
| 1 | 1×6 pin header + 1 jumper cap (J6, programming) | — | back, THT |
| 1 | LED 3/5 mm | — | back, THT |
| 1 | SSD1306 128×64 I2C OLED module (0x3C; 0x3D works after changing `address:`), or a 1.54" ST7789 240×240 IPS TFT | — | wired |
| 1 | Speaker 4 Ω 3 W (same as V2) | — | wired |

Use JLCPCB's "Basic" parts wherever the value allows. The module and the amp are "Extended" (one-time setup fee
per part per order). The module is MSL 3, which the assembler handles.

Prices (LCSC, October 2026): the module $5.19 (10+: $4.56, 100+: $3.64), the MAX98357A $1.33 (10+: $1.16, 100+:
$0.89). Together $6.52 a board at quantity 1 and $4.53 at 100+, against about $6 for the V2 parts they replace
(ESP-12F, DFPlayer, microSD card).

### 4.1 Resistors and capacitors: through-hole or SMD

Every part has a through-hole and a hand-solderable SMD option (all checked on Digi-Key on 2026-10-05); pick per
part. The SMD options are 1206 size, and their footprints are KiCad's `HandSolder` variants with longer pads:
`Resistor_SMD:R_1206_3216Metric_Pad1.30x1.75mm_HandSolder` and `Capacitor_SMD:C_1206_3216Metric_Pad1.33x1.80mm_HandSolder`.
Through-hole resistors use `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal`, the TDK ceramics
`Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm` (5 mm lead spacing).

C4 (module 3V3) and C6, C7 (amp VDD) are better as SMD whichever way you solder: they only work well right at their
pins. If you fit them through-hole, put them within a few mm of the pins with short leads.

| Ref | Value | Through-hole | SMD |
|---|---|---|---|
| R1, R2 | 5.1 kΩ 1% | Yageo MFR-25FBF52-5K1 | Yageo RC1206FR-075K1L |
| R3 (R4 optional) | 10 kΩ 1% | Yageo MFR-25FBF52-10K | Yageo RC1206FR-0710KL |
| R5 | 4.7 kΩ 1% (any 1–47 kΩ) | Yageo MFR-25FBF52-4K7 | Yageo RC1206FR-074K7L |
| R6, R7 | 100 kΩ 1% | Yageo MFR-25FBF52-100K | Yageo RC1206FR-07100KL |
| R8, R9 | 4.7 kΩ 1% | Yageo MFR-25FBF52-4K7 | Yageo RC1206FR-074K7L |
| R10, R11 | 1 kΩ 1% | Yageo MFR-25FBF52-1K | Yageo RC1206FR-071KL |
| C1 | 470 µF 10 V low-ESR | Panasonic EEU-FR1A471, 8 × 11.5 mm, `CP_Radial_D8.0mm_P3.50mm` | Panasonic EEE-FK1A471P, 8 × 10.5 mm can, `CP_Elec_8x10.5` |
| C2, C6 | 10 µF 25 V X5R | TDK FG28X5R1E106MRT06 | Samsung CL31A106KAHNFNE |
| C3 | 22 µF 25 V electrolytic (suits the AMS1117) | Panasonic ECA-1EM220, `CP_Radial_D5.0mm_P2.00mm` | Panasonic EEE-FK1E220R, 5 mm can, `CP_Elec_5x5.8` |
| C4, C7 | 100 nF 50 V X7R | TDK FG28X7R1H104KNT06 | Samsung CL31B104KBCNNNC |
| C5 | 1 µF X7R | TDK FG28X7R1E105KRT06 (25 V) | KEMET C1206C105K5RACTU (50 V) |

## 5. PCB layout

1. **Antenna:** put the module's antenna end at a board edge, ideally overhanging it. Keep copper off both layers
   under and beside the antenna (follow the keep-out drawing in the module datasheet). No back-side parts, screws
   or speaker magnet near it.
2. **One-sided assembly:** every SMD part on the front. Back-side parts are THT, soldered from the front, so the
   PCB house assembles one side only.
3. **Amplifier:**
   - Short, direct I2S traces away from the antenna.
   - Solid ground under the amp, with thermal vias in its exposed pad.
   - Bulk and 10 µF + 100 nF right at VDD.
   - Wide OUTP/OUTN traces (≥ 0.5 mm) to the speaker terminal.
   - Details in `max98357a_amplifier.md` section 6.
4. **J6:** at a board edge the enclosure can reach (or that is easy to reach with the case open); the first
   flash needs it, later updates go over Wi-Fi.
5. **Ground:** a continuous ground pour on the back layer; stitch vias around the board and the amp.
6. **Power:** +5V from USB-C to the amp and the LDO with wide traces (≥ 1 mm). The amp's peaks are about 1 A.
7. **Silkscreen:** the switch's directions (arrows and "SEL"), the OLED pin order, gain jumper settings (3/6/9/12/15 dB, "open =
   9 dB"), terminal labels (SPK +/−, RELAY SIG/GND), board name and version "Athan V3", and the GitHub URL.
8. **Mechanical:** 4 × M3 holes matching the enclosure. Put the USB-C at an edge where the enclosure has its
   opening. Place the switch and LED where the enclosure's switch opening and light pipe sit.

Before ordering: run KiCad's DRC and ERC, compare the footprint pin-out of the module and the amp with their
datasheets, and check the 3D view for anything tall on the front.

## 6. Ordering (JLCPCB)

- 2-layer, 1.6 mm, HASL lead-free or ENIG (ENIG helps the TQFN), with assembly on the **top** side.
- Export from KiCad: Gerbers + drill, the BOM (Designator, Value, Footprint, LCSC) and the CPL (placement). Check
  the rotation of the module, the amp and the USB-C in JLCPCB's preview; they are often off by 90°/180°.
- Solder the THT parts yourself, or order THT assembly.

## 7. Enclosure (3D)

Start from the V2 enclosure, `../esp_athan/hardware/encolsure/Iteration13/` (`cover13.f3d` / `.step`, round and
rectangular speaker variants, two display variants), and adapt it:

| Item | Change from V2 |
|---|---|
| PCB mount | New outline and M3 hole positions from the V3 board |
| USB-C opening | Power only, as on V2; big enough for common cable boots (≈ 12 × 7 mm) |
| J6 | Reachable with the case open (first flash only), or a small slot if you want to reflash closed |
| Removed openings | No AUX jack, no SD card slot, no screw-terminal power input, no speaker slide switch |
| Switch | An opening or a cap over the 5-way switch's stem, room to tilt it in four directions and press it in |
| LED | Light pipe or hole over the LED |
| Display | Window and clips for the module; leave room for its cable to the header (4 wires for the OLED, 8 for the TFT). The modules differ in board size and where their visible area sits, so take the window and mounting from the seller's drawing |
| Speaker | Same speaker; a sealed back volume around it sounds fuller; grille holes in front of the cone |
| Antenna | No metal, screws or speaker magnet within ≈ 15 mm of the module's antenna end; plastic only there |
| Relay terminal | Cable exit or opening for the 2-wire relay lead |
| Ventilation | A few slots; the amp and ESP get warm at full volume |
| Labels | Embossed arrows around the switch, optional QR code to the README |

Put the exported STL/STEP/F3D files in `hardware/enclosure/`.

## 8. First power-on of a new board

1. Before fitting the module, or with it fitted but before USB: check +5V and +3V3 for shorts with a meter.
2. First flash through J6: wire a 3.3 V USB-serial adapter (GND → 1, adapter RX → 4, adapter TX → 5), put the
   jumper on J6 pins 1–2, and plug the board's USB-C into a charger. Run `./flash_firmware.sh usb-full` (or
   `./build_firmware.sh flash`) and pick the adapter's port. The first flash writes the partition table. Remove
   the jumper and unplug and replug the USB-C.
3. Logs on the same adapter: `./build_firmware.sh logs /dev/cu.usbserial-…` (UART0, 115200 baud). Look for
   `Athan:` in the config dump, `Slot athan: (empty)` on a new board, and the I2C scan finding 0x3C. Once Wi-Fi is
   set up, `./build_firmware.sh logs athan.local` works without the adapter, and updates go over Wi-Fi
   (`./flash_firmware.sh ota`).
4. Set up Wi-Fi (README 1.1). Within minutes the default sounds download. Play one from the menu at 10 %, then
   raise the volume and listen for clipping (that sets the gain jumper).
5. Run the full checklist in section 9 on the first boards.

## 9. Acceptance checklist (prototype and first boards)

Run it on the breadboard prototype (section 2) before ordering boards, and again on the first boards (section 8).
A regular XIAO ESP32S3 (8 MB flash) also works for everything below except the full 3 MB sound slots: shrink them
first (microcontroller_demand.md section 4). Its 5V pin is USB VBUS directly (Seeed schematic v1.2), so the amp
breakout can take its 5 V from there. DEVELOPER.md section 11 lists the firmware assumptions behind these items.

1. Firmware size against the 0x360000 app slots (`firmware/partitions.csv`; the last OTA image was 1.78 MB).
2. Wi-Fi setup, both methods (README 1.1):
   - Bluetooth from Chrome on Android (improv-wifi.com) and from the Home Assistant app; the Select confirmation
     works; Bluetooth turns off after joining.
   - The hotspot from an iPhone; it appears after `ap_timeout` (3 min).
   - A new clock and a clock whose saved network vanished both reach setup mode (Bluetooth after 15 s); when the
     saved network returns, the clock reconnects by itself.
   - Free internal heap with Bluetooth and the hotspot on and a stored athan playing.
3. Each slot downloads from the catalog. A slot is unchanged after: pulling the network mid-download, a file over
   the size limit (3 MB, tick 0.4 MB), a file over the duration limit (5 min, tick 1 min), a file that is not MP3.
   The `/audio` page shows the reason.
4. Upload from the `/audio` page with the same four failure cases.
5. Power cut during the flash write: the slot is treated as empty, the athan time is marked with three tones, and
   the default downloads again once the clock is online (at most every 30 min).
6. The athan, Fajr athan, tawashih and tick play from flash at 10 % … 100 % volume; no hiss when idle (the amp is
   shut down). Pre-Fajr Tawashih, toggled from the web page, Home Assistant and the menu, starts 25 min before
   Fajr with the relay on, and the relay goes off 15 min after the tawashih (`prefajr_offset_min`,
   `prefajr_relay_min`). The amplifier's own checks are `max98357a_amplifier.md` section 7.
7. Radio: an mp3quran stream for an hour without dropouts; empty slots skipped; a following slot picks up a link
   changed in `docs/radio/stations.json` (within 6 h); an own link plays; after a router restart the stream comes
   back by itself (the clock gives up after 10 min).
8. Radio playing: a scheduled athan pauses it and the same station comes back when the athan ends; the Pre-Fajr
   Tawashih does the same; Stop during the athan leaves the radio off; an hourly tick plays over the lowered radio,
   and the radio is back at full level afterwards.
9. The web page, the Home Assistant API, the screen and the switch respond while a stream plays; heap and PSRAM
   stay stable over hours. The 5-way switch: each direction does what its name says as the owner faces the clock
   (else swap the pins in the yaml); every menu row works from Up/Down/Left/Right/Select; quick browsing never
   restarts the board; Left/Right change the volume of a playing athan and radio without stopping them; Up toggles
   the radio, Down the relay; both power-up gestures work; the timetable, the menu's titles and captions (Tiny5
   pixel font) and the menu's icons are readable on the real OLED; the menu goes round and Exit returns with
   Select, Left or Right.
10. An update from a GitHub Release (redirected asset URL) works; a V2 (ESP8266) clock does not see it.
11. 5 V current at full volume measured; no brownout resets; no audible Wi-Fi buzz; the gain jumper set
    (`max98357a_amplifier.md` section 4).
12. Prayer times: the scripts build a yearly file for every published year, and they pass the clock's checks. After
    the clock is set, unplugging the router for two days does not stop a single athan. Choosing another mosque
    downloads its year. With the clock set to 1 December the next year is fetched; on 1 January it is used. With
    no next-year file on the server, "not found" shows no error; from 1 January the stand-in runs (the hollow
    square on the screen, "[estimated]" on the web page) and the athan plays; publishing the file switches the
    clock over within 6 hours. Stand-in dates inside a DST window (for example 10 March) come out right, not an
    hour off. A brand-new clock in early January with no stored year fetches the previous year for the stand-in. A
    broken file (wrong year, a missing day, times out of order) is refused and the stored year stays. After a
    power cut with no internet the timetable shows the last day's times struck through, and the clock recovers by
    itself when the internet returns.
13. TFT build (`./build_firmware.sh --display tft`, J7 in 3.2): the Wi-Fi setup screen shows a teal icon and
    an orange "Press Select" on black (else set `tft_color_order`, `tft_invert`, `tft_offset_height` in
    `displays/tft.yaml`); the ring, the strip, the menu's icons and the pop-ups match the design page
    ([design page](https://claude.ai/artifact/XttPFXwoSqycdCdLjbHLoi): clock C, menu M1, Teal); every Material Symbols icon shows
    (a missing glyph draws nothing); Screen Colour changes the accent at once; a TFT clock offers only TFT releases
    (`manifest-tft.json`) and an OLED clock only OLED ones.
14. Screen settings, both displays: Brightness 10 % is still readable in a dark room and 100 % in daylight (TFT:
    gamma 2 on the backlight; OLED: contrast); 0 % turns the screen dark at once; Screen at Night Dim and Dark switch
    at Night From and back at Night Until; while dark, the first key press only lights the screen (dim) for 2 min
    with the seconds counting down in a corner, and the athan and the Pre-Fajr Tawashih light it while they play.
    `screen_dim_pct` (10 %) and `screen_wake_s` (120 s) may need tuning on the real backlight.

## 10. Options for later

None of these is needed for the clock; each is independent of the others. Other chips (5 GHz with an ESP32-C5,
32 MB flash) are in microcontroller_demand.md section 3, firmware ideas in DEVELOPER.md section 12.

| Idea | What it gives | What it takes |
|---|---|---|
| **RTC** (PCF85063A + CR1220 or a supercap) | With the year's prayer times stored, the time is the only thing that still needs the internet after a power cut; an RTC makes the clock fully offline | About $1 on I2C (with a TFT the I2C pins carry SPI: two of the free GPIOs in 3.2); ESPHome's RTC time platform, synced from SNTP, read at boot |
| **3.5 mm line out** | Feed an external speaker or amplifier | A PCM5102A DAC on the second I2S port + a jack; a second `speaker` in the chain. The MAX98357A's output is a speaker output, not line level |
| **Speaker EMI filter** | Less radio interference if the speaker ever sits on wires longer than about 30 cm, or if the prototype shows Wi-Fi or radio trouble at full volume | A ferrite bead and 680 pF C0G per output: `max98357a_amplifier.md` section 8 |
| **5 V screw-terminal input** | Hard-wired installations | A terminal + reverse-polarity protection, in parallel with the USB-C VBUS |
| **Hardware speaker mute switch** | A mute that works without the firmware | A slide switch in series with the speaker, as on V2 |
| **Louder audio** | Big rooms, small mosques | A TAS5805M-class amplifier (needs a 12–20 V supply) instead of the MAX98357A |
| **Bigger local library** (microSD) | Many recordings offline | An SDMMC socket + `esphome_sd_card` + a custom media source; gives up the one-file-per-slot design |

## 11. Links

- Module: [ESP32-S3-WROOM-1/1U datasheet](https://documentation.espressif.com/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf),
  [C2913202 at LCSC](https://www.lcsc.com/product-detail/C2913202.html),
  [at JLCPCB](https://jlcpcb.com/partdetail/3198300-ESP32_S3_WROOM_1N16R8/C2913202)
- Amplifier: [C910544 at LCSC](https://www.lcsc.com/product-detail/C910544.html),
  [at JLCPCB](https://jlcpcb.com/partdetail/MAX98357AETE+T/C910544); the circuit is in `max98357a_amplifier.md`
- Prototype: [ESP32-S3-DevKitC-1](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/),
  [Adafruit MAX98357A breakout (3006)](https://www.adafruit.com/product/3006)
