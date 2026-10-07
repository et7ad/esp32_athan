# MAX98357A amplifier: replicating the Adafruit 3006 board on the version 3 PCB

Date: 2026-10-04. Companion to `version3_planning.md` (sections 3.1, 3.3 and 3.5) and `HARDWARE.md`. In the
firmware, SD_MODE is `output: amp_enable` (GPIO15, `pin_amp_sd` in `firmware/athan.yaml`): switched on before any
playback (`amp_wake`) and off 5 s after the player goes idle (`amp_idle_off`), also when a requested sound never
started.

The version 3 board uses the bare MAX98357A chip (MAX98357AETE+T, LCSC/JLCPCB C910544) wired the way Adafruit wires
its [MAX98357A I2S amp breakout, product 3006](https://www.adafruit.com/product/3006), with three changes: SD_MODE is
driven by an ESP32-S3 GPIO through 4.7 kΩ and selects the left channel (section 5), the gain is selectable with a jumper so it can be tried out on the real speaker, and the
output EMI filter is left out because the speaker wires are only about 5 cm (it is described in section 8 in case
it is ever needed).

Sources: Adafruit's own Eagle schematic for the board
([`Adafruit MAX98357 Breakout.sch`](https://github.com/adafruit/Adafruit-MAX98357-I2S-Amp-Breakout)), the
[Adafruit guide's pinout page](https://learn.adafruit.com/adafruit-max98357-i2s-class-d-mono-amp/pinouts), and the
[MAX98357A/B datasheet](https://cdn-shop.adafruit.com/product-files/3006/MAX98357A-MAX98357B.pdf), and Maxim's own
evaluation board for the chip
([MAX98357DEV#WLP / MAX98357EVSYS#WLP data sheet](https://docs.ampnuts.ru/maximintegrated.com.datasheet/MAX98357DEV%23WLP-MAX98357EVSYS%23WLP.pdf),
[product page](https://www.analog.com/en/resources/evaluation-hardware-and-software/evaluation-boards-kits/max98357devwlp.html)),
whose parts list gives exact part numbers where Adafruit's schematic only says "Ferrite". All values below come from
these sources (compared side by side in 2.1).

## 1. The chip (TQFN-16, 3 × 3 mm, top view pin numbers)

| Pin | Name | Use here |
|---|---|---|
| 1 | DIN | I2S data from the ESP |
| 2 | GAIN_SLOT | Gain select (section 4) |
| 3, 11, 15 | GND | Ground |
| 4 | SD_MODE | Shutdown and channel select (section 5) |
| 5, 6, 12, 13 | N.C. | Leave unconnected |
| 7, 8 | VDD | Supply, 2.5–5.5 V (our 5 V rail) |
| 9 | OUTP | Speaker + |
| 10 | OUTN | Speaker − |
| 14 | LRCLK | I2S word clock from the ESP |
| 16 | BCLK | I2S bit clock from the ESP |
| EP | Exposed pad | Not connected inside; solder it to the ground plane for heat |

No MCLK is needed. Sample rates 8–96 kHz. The ESP's 3.3 V logic drives DIN/BCLK/LRCLK directly.

Order the **A** version: the MAX98357**B** uses a different data format (left-justified instead of I2S).

## 2. The Adafruit 3006 board as built (reference)

From Adafruit's schematic (rev A, 2016):

```text
                 VIN (2.5-5.5 V)
                  |
        +---------+----------+----------------+
        |         |          |                |
       C1        C2        R1 1M              |
      0.1uF     10uF         |           pins 7, 8
        |         |          |                |
       GND       GND         |      +---------+----------+
                             |      |        VDD         |
   header SD   --------------+------| 4  SD_MODE  OUTP 9 |---[FB1]---+---o terminal +
   header DIN  ---------------------| 1  DIN             |           |
   header BCLK ---------------------| 16 BCLK            |          C5 220pF
   header LRC  ---------------------| 14 LRCLK           |           |
   header GAIN ---------------------| 2  GAIN_SLOT       |          GND
                                    |                    |
                                    |            OUTN 10 |---[FB2]---+---o terminal -
                                    |        GND         |           |
                                    +--+----+----+----+--+          C4 220pF
                                       3    11   15   EP             |
                                       +----+----+----+             GND
                                              |
                                             GND
```

| Ref | Value (package) | Connection | Purpose |
|---|---|---|---|
| U1 | MAX98357A (TQFN-16) | | DAC + class-D amplifier |
| C1 | 0.1 µF ceramic (0805) | VIN to GND | High-frequency supply bypass |
| C2 | 10 µF ceramic (0805) | VIN to GND | Supply bypass |
| R1 | 1 MΩ (0805) | VIN to SD_MODE | Selects the (L+R)/2 mono mix at 5 V (section 5) |
| FB1, FB2 | ferrite bead (0805) | in series with OUTP and OUTN | EMI filter (Adafruit's note: high impedance above ~1 MHz, low at audio) |
| C5, C4 | 220 pF ceramic (0805) | after FB1 / FB2, to GND | EMI filter, with the beads |
| GAIN | not connected on the board | brought to the header | Open = 9 dB |
| X1 | 2-pin screw terminal | speaker | |
| JP1 | 7-pin header | VIN, GND, SD, GAIN, DIN, BCLK, LRC | |

The datasheet calls the outputs filterless: the ferrite beads and 220 pF capacitors are optional EMI suppression
for speaker wires, not needed for the sound itself.

On the board photo, each speaker pin has a black part and a beige part next to the terminal. The black ones look
like resistors but are the ferrite beads FB1/FB2: they carry no marking, unlike the real resistor R1, which has
`1004` (1 MΩ) printed on it. So each output has an LC filter (bead in series, 220 pF to GND), not an RC one. A series
resistor would waste speaker power (1 Ω in series with 4 Ω loses 20 %); a bead is about 0.05 Ω at audio frequencies
and hundreds of ohms at MHz, where the switching noise is.

### 2.1 Values checked against Maxim's evaluation board

| Part | Adafruit 3006 | Maxim evaluation board | Maxim's rule / note | Ours (section 3) |
|---|---|---|---|---|
| VDD bypass | 0.1 µF + 10 µF ceramic | 0.1 µF 16 V X7R + 10 µF 6.3 V X5R | datasheet: 0.1 µF + 10 µF | 0.1 µF X7R + 10 µF X5R/X7R, ≥ 10 V |
| SD_MODE for the mono mix | 1 MΩ from VIN (5 V) | 634 kΩ ±1 % from 3.3 V logic | RLARGE = 222.2 × VDDIO − 100 kΩ | not used (artifacts in testing, section 5) |
| SD_MODE for right only | (not fitted) | 226 kΩ ±1 % | RSMALL = 94 × VDDIO − 100 kΩ (210 kΩ at 3.3 V) | not used |
| SD_MODE for left only | (not fitted) | 2 kΩ to 3.3 V logic | series resistor limits pin current | 4.7 kΩ from the 3.3 V ESP GPIO (section 5) |
| Gain | pin left open (9 dB) | 5-pin jumper with two 100 kΩ ±5 % | datasheet: ±5 % resistors | 2 × 3 jumper with two 100 kΩ ±5 % |
| Output ferrite beads | "Ferrite" (0805), no part number | Murata BLM18SG331TN1D (0603): 330 Ω at 100 MHz, 1.5 A, 0.07 Ω max | 100–600 Ω at high frequency, low DC resistance, rated at least 1 A | not fitted (section 8) |
| Output capacitors | 220 pF (0805) | 680 pF C0G (TDK CGA2B1C0G2A681J) | under 1 nF, value tuned for EMI with the chosen bead | not fitted (section 8) |
| When the filter is needed | fitted | optional; ships with 0 Ω links instead of beads and no capacitors | for speaker leads longer than about 12 in (30 cm); never together with an LC class-D filter | our leads are about 5 cm: left out |

## 3. Our version (on the version 3 PCB)

Same circuit, with SD_MODE fed from an ESP GPIO, a jumper block on GAIN, and the speaker wired straight to OUTP/OUTN
(no output filter). The pins match `version3_planning.md` section 3.3.

```text
                           +5V (main rail; the 470 uF bulk cap sits next to the amp)
                            |
                  +---------+---------+
                  |         |         |
                 C1        C2         |
                0.1uF     10uF        |
                  |         |    pins 7, 8
                 GND       GND        |
                            +---------+----------+
                            |        VDD         |
   ESP GPIO7  --------------| 1  DIN      OUTP 9 |----------------o SPK +   back-side terminal,
   ESP GPIO5  --------------| 16 BCLK            |                          about 5 cm of wire
   ESP GPIO6  --------------| 14 LRCLK           |                          to the speaker
                            |                    |
   ESP GPIO15 ---[R_SD]-----| 4  SD_MODE         |
                 4.7k       |                    |
   gain block --------------| 2  GAIN_SLOT       |
   (section 4)              |            OUTN 10 |----------------o SPK -
                            |        GND         |
                            +--+----+----+----+--+
                               3    11   15   EP
                               +----+----+----+
                                      |
                                     GND        (pins 5, 6, 12, 13 unconnected)
```

### Parts, and which can be through-hole

| Ref | Value | Through-hole? | Notes |
|---|---|---|---|
| U1 | MAX98357AETE+T (C910544) | No | TQFN with exposed pad: reflow only, placed by the PCB house with the other front-side SMD parts |
| C1 | 0.1 µF, X7R, 16 V (Maxim's value) | Possible, SMD preferred | Must sit right at pins 7/8 with a short path to GND. An SMD part placed by the PCB house does that best; a THT ceramic disc works only with very short leads, a few mm from the chip |
| C2 | 10 µF, X5R/X7R ceramic, ≥ 10 V | Possible, SMD preferred | Next to C1. Maxim fits a 6.3 V part, but a ceramic loses capacitance under DC voltage, so on a 5 V rail a 10–16 V rating keeps it near 10 µF. THT option: radial MLCC or low-ESR electrolytic |
| R_SD | 4.7 kΩ | Yes | Section 5: left channel; anything from 1 kΩ to about 47 kΩ works |
| R_G1, R_G2 | 100 kΩ ±5 % or better | Yes | Gain block, section 4 (the datasheet asks for ±5 %) |
| Gain header | 2 × 3 pins, 2.54 mm, plus one jumper cap | Yes | Section 4 |
| Speaker terminal | 2-pin screw terminal | Yes | Back side, as planned, wired straight to OUTP/OUTN |

No output filter: Maxim recommends one only for speaker leads longer than about 30 cm, its own evaluation board
ships without it, and ours are about 5 cm. Section 8 has the filter for later, in case it is ever needed.

## 4. Gain

### The five settings (datasheet Table 8)

| GAIN_SLOT (pin 2) | Gain | Output at full digital scale (0 dBFS) | At 5 V into 4 Ω |
|---|---|---|---|
| 100 kΩ to VDD | 3 dB | 1.80 V rms | Never clips; maximum 0.81 W |
| Directly to VDD | 6 dB | 2.54 V rms | Never clips; maximum 1.61 W |
| **Not connected** (Adafruit default) | **9 dB** | 3.59 V rms | Clips only above −1.1 dBFS (the top 12 % of the volume range); maximum clean 2.5 W |
| Directly to GND | 12 dB | 5.07 V rms | Clips above −4.1 dBFS (above 62 % of full scale); maximum clean 2.5 W |
| 100 kΩ to GND | 15 dB | 7.16 V rms | Clips above −7.1 dBFS (above 44 % of full scale); maximum clean 2.5 W |

How the numbers come about: the datasheet defines output level (dBV) = input level (dBFS) + 2.1 dB + gain. At 5 V
into 4 Ω the cleanest the chip can do is 2.5 W at 1 % distortion (3.2 W at 10 %), which is 3.16 V rms. Above that
the waveform is cut flat by the supply voltage (clipping).

### Is maximum gain bad? Can it burn the chip?

- **It will not burn the chip.** The MAX98357A has a 2.8 A current limit (it switches the outputs off for 100 µs and
  retries), thermal-overload protection, and survives a shorted output continuously.
- **It does not make the speaker louder before distortion.** The loudest clean sound is the same 2.5 W at 9, 12 and
  15 dB; the supply voltage sets it, not the gain. Higher gain only makes clipping start lower on the volume
  scale: at 15 dB, everything above 44 % of full digital scale is distorted.
- **Sound quality:** clipping sounds harsh and crackly, worst on a loud voice like the athan. That is the real cost
  of high gain.
- **The speaker is what can suffer.** A hard-clipped signal carries more power than a clean one, partly as
  high-frequency content. Long, loud, heavily clipped playback can overheat a small 3 W speaker; the chip protects
  itself, not the speaker. At 9 dB the clean maximum (2.5 W) is below the speaker's 3 W rating.
- **Hiss is not a concern:** the datasheet's output noise is 25 µV rms even at 15 dB.
- **Power:** louder means more current. Adafruit measured about 650 mA at maximum output and recommends at least
  800 mA of supply for the amp alone; the version 3 plan already uses a 5 V 2 A supply.

### Recommended configuration

1. **Start at 9 dB (pin 2 not connected), as Adafruit ships it.** Full digital scale then lands almost exactly on
   the chip's clean maximum: the firmware volume slider covers the whole useful range, and only the last ~12 % can
   clip, and only on the loudest peaks.
2. **If 100 % volume is still too quiet, first make the recordings louder, not the gain.** Normalizing each file so
   its peaks reach about −1 dBFS (and, if needed, gentle compression when preparing the library) gives more
   loudness without distortion.
3. **12 dB is the fallback** for recordings that are quiet and that you do not want to reprocess. Then cap the
   firmware's maximum volume at roughly 60 % of full scale so the loud parts do not clip.
4. **15 dB: avoid** with this speaker at 5 V. It only makes sense for very quiet sources.
5. **6 dB or 3 dB** if the speaker turns out to be too loud or too weak for 2.5 W (for example a smaller 1–2 W
   speaker), or the room is small: they cap the maximum at 1.6 W and 0.8 W, and nothing can clip.

### Gain jumper block (so it can be tried on the real board)

A 2 × 3 pin header next to the chip. The two middle pins are both GAIN; one jumper cap selects the setting.

```text
                column A         column B     column C
             +---------------+-----------+---------------+
     row 1   |  VDD          |   GAIN    |  GND          |    jumper B1-A1 = 6 dB    jumper B1-C1 = 12 dB
             +---------------+-----------+---------------+
     row 2   |  R_G1 to VDD  |   GAIN    |  R_G2 to GND  |    jumper B2-A2 = 3 dB    jumper B2-C2 = 15 dB
             +---------------+-----------+---------------+
                            no jumper = 9 dB (default)

     R_G1 = 100 k between pin A2 and VDD,   R_G2 = 100 k between pin C2 and GND
```

- Silkscreen the dB values next to each pair. Only one jumper at a time.
- Change the jumper with the power off (the datasheet does not say whether the pin is read only at start-up).
- Keep the GAIN trace short and away from the speaker outputs and the I2S lines: in the 9 dB (open) position the pin
  sits at its own internal bias (0.4–0.6 × VDD).
- Once a setting is chosen, the header can stay (with its jumper) or a later board revision can hard-wire it.

## 5. SD_MODE: shutdown and channel

The pin has an internal 100 kΩ (±8 %) pull-down. The voltage on it selects the mode (datasheet Table 5, trip points
B0 0.08–0.355 V, B1 0.65–0.825 V, B2 1.245–1.5 V):

| Voltage on SD_MODE | Mode |
|---|---|
| below B0 (0.16 V typ) | Shutdown: outputs off, 0.6 µA |
| between B0 and B1 (0.16–0.77 V typ) | (Left + Right)/2 mono mix |
| between B1 and B2 (0.77–1.4 V typ) | Right channel only |
| above B2 (1.4 V typ) | Left channel only |

- **Adafruit:** 1 MΩ from VIN. At 5 V that gives 0.40–0.51 V over all tolerances: the mono mix.
- **Why the value depends on the voltage behind it.** R_SD and the internal 100 kΩ pull-down form a voltage divider,
  and the mono mix needs the result between B0 and B1. The datasheet's formula for that is
  RLARGE = 222.2 × V − 100 kΩ, where V is the voltage feeding the resistor:
  - from 5 V (Adafruit, resistor to VIN): 1011 kΩ, hence their 1 MΩ;
  - from a 3.3 V GPIO (ours): 634 kΩ, hence Maxim's 634 kΩ ±1 % (an E96 value; that is why it looks odd).
- **Ours (owner's decision 2026-10-06): 4.7 kΩ from ESP GPIO15 = left channel only.** The firmware's I2S output is
  `channel: mono`, which ESPHome sends as the same samples in both the left and the right slot (checked in the
  ESPHome 2026.9 source: slot mode MONO, slot mask BOTH), so left-only gets the same samples the mono mix would.
  The difference is the pin itself: the mono mix needs SD_MODE held inside a 0.3 V window through a ~560 kΩ
  resistor, a high-impedance node that noise from the amp's own switching can push across a threshold, while
  4.7 kΩ holds it at about 3.1 V, far above B2. **Tested 2026-10-06 on the breadboard prototype: the mono-mix
  mode gave audible artifacts, and 4.7 kΩ (left only) played clean.** Worst case (GPIO high at its 2.64 V minimum, internal pull-down at 92 kΩ):
  2.5 V, still above B2's 1.5 V maximum. Any resistor up to about 70 kΩ meets that; 1–47 kΩ is comfortable.
  GPIO low or floating (boot) still means shutdown.
- **The mono-mix option, for reference** (the earlier choice; the rest of this list explains it): 560 kΩ ±5 % puts
  the pin at 0.50 V typical, the middle of the window, and 0.45–0.56 V over all tolerances.
- **Which values work from a 3.3 V GPIO** (worst-case window 0.355–0.65 V: above B0's maximum, below B1's minimum):

  | R_SD from 3.3 V | Voltage on SD_MODE (worst cases) | Result |
  |---|---|---|
  | 470 kΩ ±5 % | 0.52–0.64 V | mono on every chip, but close to the right-channel threshold |
  | 499 kΩ ±1 % or 510 kΩ ±5 % ("0.5 M") | 0.48–0.60 V | mono on every chip |
  | 560 kΩ ±5 % | 0.45–0.56 V | mono on every chip (the earlier choice) |
  | 634 kΩ ±1 % (Maxim) | 0.42–0.48 V | mono on every chip |
  | 680 kΩ ±5 % | 0.38–0.47 V | mono on every chip |
  | 750 kΩ ±5 % | 0.35–0.43 V | can read as shutdown on some chips |
  | **1 MΩ** | 0.27–0.34 V | **unreliable:** works on a typical chip (B0 0.16 V) but is silent on chips whose B0 sits near its 0.355 V maximum |

  The safe range is about 465–725 kΩ for 5 % resistors (445–755 kΩ for 1 %). Too large a resistor is the
  dangerous direction (silence). Too small selects the right channel, which still sounds right if the firmware
  sends the same mono samples to both channels.
- **Without a GPIO:** 1 MΩ from the 5 V rail, exactly as Adafruit does it, also works. The amp is then always
  enabled and drops to standby (340 µA) by itself whenever the ESP stops the I2S clock. That frees GPIO15 but loses
  the guaranteed shutdown during boot.
- GPIO high → left channel (4.7 kΩ; the earlier 560 kΩ gave the mono mix), amp on. GPIO low → shutdown. While the ESP boots, the GPIO floats and the internal pull-down
  keeps the amp in shutdown, so it stays silent during boot with no extra part.
- Alternatives:
  - Left only: GPIO to SD_MODE through 2 kΩ, as on Maxim's board (3.3 V is above B2; with VDD at 5 V the series
    resistor is not strictly required, but it costs nothing and protects the pin).
  - Right only: 226 kΩ ±1 % from the 3.3 V GPIO (Maxim's board; the formula gives 210 kΩ).
- Firmware side: raise SD before playback; after playback, let the audio fade or end, then pull it low (the
  datasheet: the chip has no ramp-down on entering shutdown, so the data should go quiet first). Without the GPIO the
  chip also drops to standby by itself (340 µA) when BCLK stops.
- Settled: left channel only. The firmware sends the same mono samples in both slots, so the level matches the
  mono mix, but the mono mix had artifacts and left-only did not (tested 2026-10-06). The mono samples are
  themselves the left channel: ESPHome's mixer keeps channel 0 of a stereo source. So a sound plays in full only if
  it is mono or has its whole content in the left channel (README 1.5).

## 6. Layout

- Exposed pad soldered to a solid ground pour with several thermal vias to the other layer; GND pins 3, 11 and 15
  on the same ground.
- C1 closest to pins 7/8, C2 right after it, both with the shortest possible return to the GND pins.
- Wide traces for VDD, GND, OUTP and OUTN. The datasheet's example: 100 mΩ of speaker trace loses about 5 % of 2 W
  into 4 Ω, 10 mΩ loses about 2.5 %.
- Run OUTP and OUTN side by side, short and wide, straight to the speaker terminal. Keep them away from the ESP
  module's antenna and from the I2S lines.
- Every pF on the output traces costs idle current (VDD × 330 kHz × C), so keep them short.
- Twist the two speaker wires together (or keep them side by side); with about 5 cm of wire that is all the EMI care
  needed.
- I2S traces (BCLK, LRCLK, DIN) short, with ground fill around them (the datasheet recommends ground fill around all
  signal traces).
- Speaker: 4 Ω or more, with a voice-coil inductance above 10 µH (normal for real speakers). Never connect OUTP or
  OUTN to ground or to another amplifier's input: the outputs are bridge-tied.

## 7. Prototype checks (feed into checklist item 6 of `version3_planning.md`)

1. 9 dB: play the loudest athan at 100 %. Listen for crackle on the loudest syllables; note how loud it is in the
   room.
2. If it is too quiet: check the file's peak level first (normalize to about −1 dBFS), then try 12 dB with the
   firmware volume capped at ~60 %, and compare by ear.
3. Silence playing at each gain setting: confirm no audible hiss.
4. Ten minutes at the chosen maximum: the speaker should be warm at most, and the 5 V rail should not dip enough to
   reset the ESP.
5. Left channel: with 4.7 kΩ, SD_MODE measures about 3 V while playing and about 0 V when idle (section 5).
6. Boot and shutdown: no pop at power-up, during ESP boot, or when SD goes low after playback.
7. Interference: with the athan playing loud, the Wi-Fi stays connected and a radio stream does not stutter, and a
   nearby AM/FM radio does not pick up a buzz. If any of that fails, add the filter from section 8.

## 8. Optional, for later: output EMI filter

Left out of the baseline because the speaker wires are about 5 cm; Maxim recommends it only above about 30 cm.
Add it if the prototype shows interference (checklist item 7), if a later enclosure puts the speaker on longer
wires, or if the device ever needs EMC testing.

```text
   OUTP 9 ---[FB1]---+---o SPK +
                     |
                    C5
                     |
                    GND
   OUTN 10 ---[FB2]---+---o SPK -
                      |
                     C4
                      |
                     GND
```

| Ref | Value | Notes |
|---|---|---|
| FB1, FB2 | Murata BLM18SG331TN1D (0603): 330 Ω at 100 MHz, 1.5 A, 0.07 Ω max | Maxim's part. Maxim's rule: 100–600 Ω at high frequency, low DC resistance, rated at least 1 A (the full speaker current flows through it: 0.79 A rms at 2.5 W into 4 Ω, peaks above 1 A). Through-hole beads in that range exist (for example Würth's leaded WE-WAFB family) |
| C4, C5 | 680 pF C0G/NP0, ≥ 50 V | Maxim's value. Under 1 nF; Adafruit uses 220 pF with its bead, also inside the rule. Through-hole ceramic disc C0G is fine |

- The beads in Adafruit's generic Eagle library list are signal beads rated only 200–600 mA, below Maxim's 1 A
  minimum, so do not copy those.
- Place the beads and capacitors at the speaker terminal, as Adafruit does.
- Cost: about 2.2 mA of extra idle current (5 V × 330 kHz × 2 × 680 pF).
- Never combine it with an LC class-D output filter (Maxim's note).
- A cheap hedge, if wanted, on the next board revision: lay out the bead footprints fitted with 0 Ω links and the
  capacitor footprints left empty, exactly as Maxim ships its evaluation board. The filter can then be added later
  without a new board.
