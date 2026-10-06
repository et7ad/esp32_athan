# Hardware: board, enclosure, wiring

Instructions for drawing the version 3 PCB in KiCad (`hardware/pcb/`) and the enclosure (`hardware/enclosure/`),
and for wiring a breadboard prototype first. The design decisions behind them are in `version3_planning.md`
sections 3 and 13. The amplifier circuit is in `max98357a_amplifier.md`. The firmware pin assignments are the
`pin_*` substitutions at the top of `firmware/athan.yaml`, so change both together.

Status: nothing is drawn yet. Build the prototype (section 2) and run its checklist before ordering boards.

## 1. Block diagram

```text
USB-C 6-pin (5 V only) ──┬── 5 V rail ──┬── MAX98357A (VDD) ── speaker terminal (OUTP/OUTN, 4 Ω 3 W)
   CC1/CC2 5.1 kΩ        │   bulk cap   │        ▲ I2S BCLK/LRCLK/DIN, SD_MODE (560 kΩ)
                         │              │        │
                         └── LDO 3.3 V ─┴── ESP32-S3-WROOM-1-N16R8 ── I2C ── OLED header (SSD1306)
                                                 ├── Next / Select buttons (to GND)
                                                 ├── status LED
                                                 ├── relay output terminal (logic signal + GND)
                                                 └── UART0 + EN + IO0 ── J6 programming header
```

## 2. Prototype on a breadboard (do this first)

Parts: **ESP32-S3-DevKitC-1 N16R8**, an **Adafruit MAX98357A breakout (3006)** or a clone, the speaker, a
128×64 SSD1306 I2C OLED, two push buttons, an LED + 1 kΩ, and a 560 kΩ resistor (any 470–680 kΩ).

| From (DevKitC pin) | To | Notes |
|---|---|---|
| 5V | breakout **Vin** | DevKitC's 5V pin carries USB VBUS when powered over USB |
| GND | breakout **GND**, OLED GND, buttons, LED cathode | common ground |
| GPIO5 | breakout **BCLK** | |
| GPIO6 | breakout **LRC** | |
| GPIO7 | breakout **DIN** | |
| GPIO15 | breakout **SD**: see the note below | |
| breakout **GAIN** | open (9 dB) | other gains: `max98357a_amplifier.md` section 4 |
| breakout **+ / −** | speaker | |
| 3V3 | OLED VCC | |
| GPIO8 / GPIO9 | OLED SDA / SCL | most OLED modules already carry 4.7–10 kΩ pull-ups |
| GPIO10 | Next button → GND | internal pull-up |
| GPIO11 | Select button → GND | internal pull-up |
| GPIO12 | 1 kΩ → relay input (or an LED to see it) | |
| GPIO13 | 1 kΩ → LED → GND | status LED |

**SD on the breakout:** the Adafruit board already has 1 MΩ from SD to Vin, which keeps the amp on in mono mode.
Do **not** add our 560 kΩ next to it. Together they put SD at about 0.4 V with GPIO15 low (amp still on) and
about 0.85 V with GPIO15 high (right channel only). Pick one:

- **Simplest:** leave SD unconnected and GPIO15 unused. The amp is always on (it idles in standby when I2S stops).
  Fine for everything except the "no hiss / no pop" checks.
- **Faithful to the PCB:** remove the breakout's 1 MΩ (or cut its trace), then wire GPIO15 → 560 kΩ → SD.
  Clones vary; measure SD first.

Flash it over either DevKitC port and watch the log on its **UART** port: the firmware logs on UART0, which on the
final board is the J6 header. Then run `version3_planning.md` section 13. The firmware, partition table and pins
are the same as on the final board.

## 3. Schematic (KiCad)

Draw it as four blocks. Values come from the Espressif module datasheet (v1.8, "peripheral schematics"), the
Adafruit 3006 board and the plan. Net names in **bold** match the firmware.

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
| **AMP_SD** | 15 | 560 kΩ → MAX98357A SD_MODE (pin 4) |
| **I2C_SDA** | 8 | OLED header SDA, 4.7 kΩ to +3V3 |
| **I2C_SCL** | 9 | OLED header SCL, 4.7 kΩ to +3V3 |
| **BTN_NEXT** | 10 | Next button to GND |
| **BTN_SELECT** | 11 | Select button to GND |
| **RELAY_OUT** | 12 | 1 kΩ → relay terminal pin 1 (pin 2 = GND) |
| **LED** | 13 | 1 kΩ → LED → GND |
| **TXD0 / RXD0** | 43 / 44 | J6 pins 4 / 5 (UART0: flashing and logs) |

### 3.3 Amplifier

Copy `max98357a_amplifier.md` section 3 exactly:

- MAX98357AETE+T (LCSC C910544), TQFN-16 with exposed pad to GND.
- 10 µF + 100 nF at VDD.
- SD_MODE through 560 kΩ from GPIO15.
- 2×3 gain jumper header (no jumper = 9 dB). As `Conn_02x03_Odd_Even`: pins 3 and 4 = GAIN, 2 = +5V (2–4: 6 dB),
  6 = GND (4–6: 12 dB), 1 = 100 kΩ to +5V (1–3: 3 dB), 5 = 100 kΩ to GND (3–5: 15 dB).
- OUTP/OUTN straight to the speaker terminal, no output filter.

Add footprints for the optional EMI filter (section 8 there) only if you want the option later, populated with
0 Ω / DNP.

### 3.4 Back-side parts (through-hole, soldered from the front)

The same footprints as the V2 board (`../esp_athan/hardware/pcb/athan_kicad`):

| Part | Footprint (V2) |
|---|---|
| Next, Select buttons | `Button_Switch_THT:SW_SPST_Omron_B3F-40xx` |
| Speaker terminal, relay terminal | `TerminalBlock:TerminalBlock_MaiXu_MX126-5.0-02P_1x02_P5.00mm` |
| OLED header 1×4 (GND, VCC, SCL, SDA) | 2.54 mm pin header. **Print the order on the silkscreen**; modules differ (GND-VCC or VCC-GND first) |
| Status LED | 3 mm or 5 mm THT LED |
| Mounting holes | `MountingHole_3.2mm_M3` × 4 |

Removed compared with V2: DFPlayer, microSD, 3.5 mm jack, the on-board display footprint, speaker slide switch,
5 V screw-terminal input. Kept: the 6-pin power-only USB-C (now with CC resistors) and a programming header (J6,
1×6, new pin order).

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
| 1 | 560 kΩ ±5 % or better (SD_MODE; safe range 470–680 kΩ, never 1 MΩ) | Basic/Extended | front |
| 1 | 1 µF (EN) | Basic | front |
| 2 | 100 nF (3V3, amp VDD) | Basic | front |
| 1 | 22 µF (3V3 at the module, also the LDO output cap) | Basic | front |
| 2 | 10 µF (LDO in, amp VDD) | Basic | front |
| — | ≈ 470 µF bulk on 5 V (one SMD cap or several ceramics) | — | front |
| 1 | 2×3 pin header + 1 jumper (gain) | — | front or back |
| 2 | Omron B3F-40xx push buttons | — | back, THT |
| 2 | MX126 2-pin 5.0 mm terminals | — | back, THT |
| 1 | 1×4 pin header (OLED) | — | back, THT |
| 1 | 1×6 pin header + 1 jumper cap (J6, programming) | — | back, THT |
| 1 | LED 3/5 mm | — | back, THT |
| 1 | SSD1306 128×64 I2C OLED module (0x3C; 0x3D works after changing `address:`) | — | wired |
| 1 | Speaker 4 Ω 3 W (same as V2) | — | wired |

Use JLCPCB's "Basic" parts wherever the value allows. The module and the amp are "Extended" (one-time setup fee
per part per order). The module is MSL 3, which the assembler handles.

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
| R5 | 560 kΩ 1% (any 470–680 kΩ) | Yageo MFR-25FBF52-560K | Yageo RC1206FR-07560KL |
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
7. **Silkscreen:** button names (Next, Select), the OLED pin order, gain jumper settings (3/6/9/12/15 dB, "open =
   9 dB"), terminal labels (SPK +/−, RELAY SIG/GND), board name and version "Athan V3", and the GitHub URL.
8. **Mechanical:** 4 × M3 holes matching the enclosure. Put the USB-C at an edge where the enclosure has its
   opening. Place the buttons and LED where the enclosure's button caps and light pipe sit.

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
| Buttons | Two button caps or plungers over the back-side buttons (Next, Select) |
| LED | Light pipe or hole over the LED |
| OLED | Window and clips for the 128×64 module; leave room for the 4-wire cable to the header |
| Speaker | Same speaker; a sealed back volume around it sounds fuller; grille holes in front of the cone |
| Antenna | No metal, screws or speaker magnet within ≈ 15 mm of the module's antenna end; plastic only there |
| Relay terminal | Cable exit or opening for the 2-wire relay lead |
| Ventilation | A few slots; the amp and ESP get warm at full volume |
| Labels | Embossed "Next" / "Select", optional QR code to the README |

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
4. Set up Wi-Fi (README 1.1). Within minutes the default sounds install. Play one from the menu at 10 %, then
   raise the volume and listen for clipping (that sets the gain jumper).
5. Run the full checklist in `version3_planning.md` section 13 on the first boards.
