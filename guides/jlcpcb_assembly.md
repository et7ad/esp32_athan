# Ordering assembled boards from JLCPCB

JLCPCB can make the board and also solder the parts onto it (PCBA, printed circuit board assembly). For that it needs
three files from KiCad: the board itself (Gerbers and drill files), a bill of materials that names a JLCPCB part for
every reference, and a placement list (the "pick and place" file) that says where each part sits and how it is
turned. Sections 1 to 6 apply to any KiCad project; section 7 covers what is particular to this clock's board.

1. [The three files](#1-the-three-files)
2. [Choosing the service](#2-choosing-the-service)
3. [Preparing the project](#3-preparing-the-project)
4. [Exporting](#4-exporting)
5. [Ordering and checking](#5-ordering-and-checking)
6. [Plugins that do most of it](#6-plugins-that-do-most-of-it)
7. [This board](#7-this-board)

## 1. The three files

| File | What JLCPCB reads from it | Made in KiCad with |
|---|---|---|
| Gerber and drill zip | The board: copper, solder mask, silkscreen, outline, holes, and the paste layers that become the solder stencil | PCB Editor: **File → Plot**, then **Generate Drill Files** in the same dialog |
| BOM (bill of materials), CSV | One line per distinct part: its value (**Comment**), its references (**Designator**), its footprint and its **LCSC part number** | Schematic Editor: **Tools → Generate Bill of Materials** |
| CPL (component placement list, the pick-and-place file), CSV | One line per placed part: **Designator**, **Mid X**, **Mid Y**, **Layer** (Top or Bottom) and **Rotation** | PCB Editor: **File → Fabrication Outputs → Component Placement** |

JLCPCB places parts from its own stock, which is the LCSC catalogue. A part is identified by its LCSC number, a C
followed by digits (for example C6186), and only a part with such a number can be placed.

## 2. Choosing the service

- **Economic or standard assembly.** The economic service is cheaper and assembles one side of the board; the
  standard service can do both sides. Both take through-hole parts (soldered on a wave soldering machine) and mixed
  parts: surface-mount parts with through-hole pegs, such as many USB-C receptacles, go through the surface-mount
  process (JLCPCB's part page says "SMT Assembly"). The side and the limits of each service show on the order page
  when you pick one; they change now and then. Source: JLCPCB's
  [assembly capabilities](https://jlcpcb.com/capabilities/pcb-assembly-capabilities).
- **Basic and extended parts.** Basic parts stay loaded on the machines. Every *different* extended part adds a
  one-time loading fee per order (a few dollars), whatever the quantity. Basic resistors and capacitors keep the price
  down.
- **What you solder yourself.** Any part can be left out and soldered by hand afterwards: tall through-hole parts,
  connectors you already have, parts JLCPCB does not stock. For a handful of boards, soldering through-hole parts
  yourself usually costs less than paying per joint.

## 3. Preparing the project

**Part numbers.**
- Find each part in JLCPCB's parts library (jlcpcb.com/parts). Filter by package (0805, 1206, SOT-223 and so on),
  prefer **Basic**, check the stock, and compare the part's drawing with your footprint: pad size, pitch and pin 1.
  For ICs and connectors, compare against the datasheet.
- Add a field named **LCSC** to every symbol JLCPCB should place, holding that number. The quickest way is the Symbol
  Fields Table (**Tools → Edit Symbol Fields**): add the column once, then fill it per group, since identical parts
  (every 4.7 kΩ 1206 resistor, say) take the same number.

**What must not be placed.**
- Mark a symbol **Do not populate** (DNP, in its properties) when its place should stay empty: optional parts, parts
  you solder yourself, modules you wire by hand. It stays on the board and in the schematic, and the exports leave it
  out.
- Footprints that are not parts (mounting holes, logos, solder jumpers, test points) get **Exclude from bill of
  materials** and **Exclude from position files** in their footprint properties.
- KiCad 10's exports also take a variant: a named set of DNP marks, for example one for a machine-assembled board and
  one for hand assembly.

**One origin for all three files.** KiCad draws the board somewhere on a page. The Gerbers, the drill files and the
placement file must all use the same origin, or the parts land beside their pads. Either keep page coordinates in all
three, or put the drill/place origin on the board's bottom-left corner (**Place → Drill/Place File Origin**) and tick
"Use drill/place file origin" in all three exports.

**Before exporting**, make sure that:
- DRC shows no errors;
- the board is updated from the schematic (**Tools → Update PCB from Schematic**);
- pin-1 and polarity marks are on the silkscreen, since you check JLCPCB's preview against them.

## 4. Exporting

**Gerbers and drill files** (**File → Plot**):
- Plot as Gerber, with the layers F.Cu, B.Cu, F.Paste, B.Paste, F.Silkscreen, B.Silkscreen, F.Mask, B.Mask and
  Edge.Cuts.
- Turn on "Subtract soldermask from silkscreen", and set "Use drill/place file origin" as decided above.
- **Generate Drill Files**: Excellon, millimetres, the same origin. Separate files for plated and non-plated holes are
  fine.
- Zip the files themselves. On a Mac, Finder's **Compress** adds a hidden `__MACOSX` folder; `zip -j -X board.zip *.gbr
  *.drl` in Terminal does not.

**BOM** (**Tools → Generate Bill of Materials**):
- Fields and their labels: Value as `Comment`, Reference as `Designator`, Footprint as `Footprint`, LCSC as `LCSC
  Part #`.
- Group by Value, Footprint and LCSC, so each distinct part is one line.
- Leave out DNP symbols, and leave the range delimiter empty: JLCPCB needs `R3,R4,R5`, not `R3-R5`.
- Save as CSV.

**Placement** (**File → Fabrication Outputs → Component Placement**):
- CSV, millimetres, both sides in one file, the same origin as the Gerbers.
- Include only SMD footprints, and leave out footprints marked DNP. Prefer "only SMD" over "exclude footprints with
  through-hole pads": some surface-mount parts have plated holes in their pads (modules with thermal vias, USB-C
  shells with pegs), and that option drops them. "Only SMD" also drops footprints that are neither SMD nor
  through-hole, such as holes, logos and solder jumpers.
- KiCad writes the header `Ref,Val,Package,PosX,PosY,Rot,Side`. Rename it to
  `Designator,Val,Package,Mid X,Mid Y,Rotation,Layer`. The sides can stay `top` and `bottom` as KiCad writes them
  (the JLCPCB Tools plugin writes them the same way). The upload page can also assign the columns by hand.

**From the command line**, the same files for a script. kicad-cli can rewrite the `.kicad_pro` next to the board, so
run it on a copy of the project:

```bash
kicad-cli pcb export gerbers --layers F.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts \
    --subtract-soldermask -o gerbers/ board.kicad_pcb
kicad-cli pcb export drill --excellon-units mm -o gerbers/ board.kicad_pcb
kicad-cli sch export bom --fields 'Value,Reference,Footprint,LCSC' \
    --labels 'Comment,Designator,Footprint,LCSC Part #' --group-by 'Value,Footprint,LCSC' \
    --ref-range-delimiter '' --exclude-dnp -o bom.csv board.kicad_sch
kicad-cli pcb export pos --format csv --units mm --side both --smd-only --exclude-dnp -o cpl.csv board.kicad_pcb
```

If you set a drill/place origin, add `--use-drill-file-origin` to the Gerber and placement commands and
`--drill-origin plot` to the drill command.

## 5. Ordering and checking

1. On jlcpcb.com, upload the Gerber zip and set the board options (layers, thickness, colour, surface finish).
2. Switch on **PCB Assembly**, then choose the service, the side to assemble and how many boards to assemble.
3. Upload the BOM and the CPL. JLCPCB matches every BOM line to a part. Check each match (value, package, Basic or
   Extended, stock). A line without a match needs a part chosen, or "do not place".
4. Check the placement preview part by part: pin 1 of every IC, the polarity of diodes, LEDs and electrolytic
   capacitors, and which way the connectors face.
   - KiCad and JLCPCB do not always agree on a package's zero rotation. SOT-223, SOT-23, QFN, electrolytic
     capacitors and many connectors are the usual suspects, so a part can show up turned by 90° or 180°.
   - Rotate such a part in the preview, and note the correction for the next order.
   - KiCad writes each footprint's origin, which is the part's centre for most footprints but not for some
     connectors and modules. Such a part shows up shifted; move it in the preview.
   - Most mistakes are on the bottom side: check those parts twice.
5. Answer the engineers' questions. Before building, JLCPCB emails about anything unclear, such as an orientation or
   a part out of stock.

## 6. Plugins that do most of it

Both install from KiCad's **Plugin and Content Manager**:
- **JLCPCB Tools** (kicad-jlcpcb-tools): searches JLCPCB's parts library from inside KiCad, assigns LCSC numbers,
  marks parts to leave out, applies known rotation corrections, and writes the Gerber zip, BOM and CPL in JLCPCB's
  format.
- **Fabrication Toolkit**: one button writes the same three files from fields on the symbols (the LCSC number, plus a
  rotation offset where a part needs one).

With either one, section 4 is done for you; the checks in section 5 still apply.

## 7. This board

The project is set up for JLCPCB, and one script makes every file.

**Making the files.** In `hardware/pcb/`, run `./generate_fab_files.sh` (KiCad need not be open). It empties
`fab_files/` and makes everything again from the saved project, with the settings of KiCad's own dialogs:
- `fab_files/athanV3.zip`: the Gerbers and drill files, to upload as the board.
- `fab_files/jlcpcb/`: the BOM and CPL for JLCPCB, `_back` for an order where JLCPCB fits the back only and
  `_both` for both sides.
- `fab_files/reports/`: DRC and ERC reports. The script prints the error counts and warns if any is not zero.
- `Bill_of_Material.csv` and `Bill_of_Material_Goup_By_Description.csv` next to the project, and
  `generated_imgs_and_3d/esp32_athan.step`.

`./generate_fab_files.sh --verify` also downloads each assembled part's footprint from JLCPCB's library and checks
pad by pad that it lands on the board's footprint at the position and rotation in the CPL. Run it after choosing a
different part. `--no-step` skips the 3D model.

**Choosing what JLCPCB places.** `generate_fab_files.sh` opens a window before it writes the JLCPCB files: the
board's front and back, drawn from the KiCad file in the solder mask colour you pick, with every part on it.
- Green parts are in the JLCPCB order. Click one to take it out (red, crossed: you solder it yourself), click again
  to put it back. The list on the right does the same, with each part's LCSC number, Basic or Extended, and stock.
- An orange border marks a part JLCPCB cannot supply now (out of stock or no longer sold), checked online when the
  window opens. Dashed grey parts are soldered by hand anyway (through-hole, DNP, or no LCSC number).
- The USB-C J1 fits either side, so it shows on both: in colour where it is fitted, dotted blue on the other side.
  Click it there to move it to that side.
- To solder a missing part before the machine-placed ones, take its neighbours out too: click the part, then
  **Take out its neighbours** (within the distance you set). Soldering it first is easier with nothing around it.
- Drag to move the board, the wheel or the buttons to zoom and turn it.
- **Save and continue** keeps the choice in `hardware/pcb/jlcpcb_selection.json` and writes the files without
  those parts; the parts left out are listed in `fab_files/README.md`. Closing the window keeps the previous
  choice, and `--no-gui` skips the window.

**The suggested order: JLCPCB assembles the back only** (the `_back` files).
- The back holds the hard parts. The amplifier is a 3 × 3 mm QFN with 0.5 mm pins and a pad underneath, and the
  module's ground pad sits under the module.
- One-side assembly is the cheaper service. When ordering, check that the economic service offers the bottom side
  for this board; if it offers only the top, order the back in the standard service.
- The USB-C J1 goes on the back in this order, so its small pads are machine-soldered too.
- The front's four surface-mount parts (C1, D1, R1, R2) are large and quick to solder by hand, along with the
  through-hole parts.

The `_both` files add the front's surface-mount parts, for the standard service.

**The USB-C, front or back.** J1's footprint has the same pads on the front and the back, and the receptacle's four
shell legs go through shared holes, so it can be soldered on either side.
- JLCPCB lists it (C3151650) for surface-mount assembly, its through-hole shell pegs included. The selection
  window chooses the side (the back unless you click it on the front), saved as `"sides"` in
  `jlcpcb_selection.json`. Taken out of the order, it can be hand-soldered on either side.
- On the back the receptacle lies flipped, so its A and B pins land on each other's pads: CC1 and CC2 trade places
  (each has its own 4.7 kΩ to ground), VBUS and GND stay on their nets.
- Its opening is at the board's left edge either way. On the back it sits about 5 mm lower, below the board: the
  enclosure's opening has to match the side.

**The parts.** Every part JLCPCB can place has its LCSC number in the schematic's **LCSC** field.

| Ref | Part | LCSC | Library |
|---|---|---|---|
| U1 | ESP32-S3-WROOM-1-N16R8 | C2913202 | extended |
| U3 | AMS1117-3.3, SOT-223 | C6186 | basic |
| U4 | MAX98357AETE+T, TQFN-16 | C910544 | extended |
| R1 to R5, R8, R9 | 4.7 kΩ 1 %, 1206 (UNI-ROYAL 1206W4F4701T5E) | C17936 | basic |
| R10 | 100 Ω 1 %, 1206 (UNI-ROYAL 1206W4F1000T5E) | C17901 | basic |
| R7 (DNP) | 100 kΩ 1 %, 1206 | C17900 | basic |
| R11 (DNP) | 100 Ω 1 %, 1206 | C17901 | basic |
| C2, C3, C6 | 22 µF 25 V X5R, 1206 (Samsung CL31A226KAHNNNE) | C12891 | basic |
| C4, C7 | 100 nF 50 V X7R, 1206 (Samsung CL31B104KBCNNNC) | C24497 | basic |
| C5 | 1 µF 50 V X7R, 1206 (Samsung CL31B105KBHNNNE) | C1848 | basic |
| C1 (front) | 470 µF 10 V, 8 mm (Panasonic EEE-FK1A471P) | C178530 | extended |
| D1 (front) | blue LED, 1206 (NationStar NCD1206B1) | C130717 | extended |
| J1 (back or front) | USB-C 6-pin (GCT USB4125-GF-A) | C3151650 | extended |
| J2 | barrel jack (CUI PJ-102AH), through-hole; a fallback, left off a board that goes in the case | C3096093 | extended |
| J3, J5 | 2-pin screw terminal (MaiXu MX126-5.0-02P), through-hole | C5188434 | extended |

- R7 and R11 are marked **DNP**: they are fitted by hand only when needed (15 dB gain; a resistor in series with the
  relay output). The parts lists still name them; the JLCPCB files leave them out.
- Each different extended part adds one loading fee: three for a back-only order (U1, U4, J1), five for both sides
  (also C1, D1). Every resistor and capacitor is a basic part.
- Not in JLCPCB's library, so always by hand: the switch U2 (a 10 × 10 mm 5-way switch, SKQUCAA010 type), the header J6 and the display header.

**What the files take care of.**
- Each part's position is the centre of its pads, which is where JLCPCB centres its own footprint (the USB-C's
  KiCad origin, for example, sits 1.1 mm off its centre).
- Two parts sit turned in JLCPCB's library against KiCad's footprint, and the CPL turns them back: the regulator U3
  by 180° and the amplifier U4 by 270°.
- J1 on the back is written as the same receptacle flipped over (bottom side, 90°), and `--verify` checks it
  against JLCPCB's footprint pad by pad like every other part.
- The LED's footprint in JLCPCB's library has its cathode on pad 1, as KiCad's does, so it needs no turn. (Some
  LEDs there number the cathode 2: `--verify` matches pads by number, so check the polarity in the preview after
  choosing a different LED.)
- The CPL holds only surface-mount parts. "Only SMD" keeps the module, whose ground pad has nine small plated holes.
  "Exclude footprints with through-hole pads" would drop it, so don't use that option for this board.
- The coordinates are the Gerbers' own (page coordinates, the board 100 mm right of and below the page's corner).

**In the preview, still check these.** The preview shows the back from below.
- U4: its pin-1 dot sits in the corner marked on the silkscreen.
- U1: its antenna end lies at the board's bottom edge, over the area without copper.
- U3: its wide tab points toward the module.
- J1: its opening at the board's edge (seen from below, the right edge). JLCPCB may add edge rails for the
  machines there; they are cut off afterwards.
- For a front order also D1 and C1 (polarity).

The resistors and ceramic capacitors have no polarity.

**Vias in pads.** U4's exposed pad has a via in it, and U1's ground pad has nine small holes. During reflow, solder
can run down an open hole and leave the joint short of solder. For a few boards this usually works out; JLCPCB's via
plugging or filling options avoid it, at extra cost.

**C1's pads** are longer than JLCPCB's footprint for this capacitor, for hand soldering. Its terminals still land on
them, about 0.4 mm further in.
