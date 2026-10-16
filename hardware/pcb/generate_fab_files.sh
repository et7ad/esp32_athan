#!/bin/bash
# Makes every fabrication and assembly file of the board from the saved KiCad project, without opening KiCad.
#
#   fab_files/                    emptied first, then:
#     athanV3/                    Gerbers, drill files and drill maps, Gerber job file
#     athanV3.zip                 the same files: upload this to JLCPCB as the board
#     jlcpcb/                     BOM and CPL (pick and place) for JLCPCB assembly: _back (JLCPCB fits the back only)
#                                 and _both (both sides)
#     reports/                    DRC and ERC reports
#     athanV3_all_files.zip       everything above in one archive
#     README.md                   which file goes where
#   Bill_of_Material.csv, Bill_of_Material_Goup_By_Description.csv     parts lists, next to the project
#   generated_imgs_and_3d/esp32_athan.step                             3D model of the assembled board
#
# The settings are the ones the KiCad dialogs use: Plot takes the settings saved in the board (File -> Plot), the
# drill files and the STEP model use the same options as in their dialogs. kicad-cli works on a copy of the project,
# so it never rewrites the project file here.
#
# Before the JLCPCB files, a window shows the board's front and back with every part: click a part to take it out of
# the JLCPCB order (you solder it yourself), for example a part JLCPCB cannot supply and its neighbours. "Save and
# continue" keeps the choice in jlcpcb_selection.json; closing the window keeps the previous one.
#
# Usage: ./generate_fab_files.sh [--verify] [--no-step] [--no-gui]
#   --verify   also check every assembled part against its footprint in JLCPCB's library (downloads them)
#   --no-step  skip the STEP model (the slowest step)
#   --no-gui   don't open the window: use the choice saved in jlcpcb_selection.json
set -euo pipefail

NAME=athanV3                  # name of the Gerber folder and zip
PROJECT=esp32_athan
KICAD_APP=${KICAD_APP:-/Applications/KiCad/KiCad.app}
CLI="$KICAD_APP/Contents/MacOS/kicad-cli"
KPY="$KICAD_APP/Contents/Frameworks/Python.framework/Versions/Current/bin/python3"

HERE="$(cd "$(dirname "$0")" && pwd)"
FAB="$HERE/fab_files"
VERIFY=""; STEP=1; GUI=1
SELECTION="$HERE/jlcpcb_selection.json"
for a in "$@"; do
  case "$a" in
    --verify) VERIFY="--verify" ;;
    --no-step) STEP=0 ;;
    --no-gui) GUI=0 ;;
    *) echo "unknown option: $a"; exit 1 ;;
  esac
done
[ -x "$CLI" ] || { echo "kicad-cli not found at $CLI (set KICAD_APP)"; exit 1; }
[ -x "$KPY" ] || { echo "KiCad's Python not found at $KPY"; exit 1; }
if ls "$HERE"/~"$PROJECT".*.lck >/dev/null 2>&1; then
  echo "Note: KiCad has the project open. Only what is saved goes into the files."
fi

# a private copy of the project for kicad-cli
TMP="$(mktemp -d -t fabfiles)"
trap 'rm -rf "$TMP"' EXIT
cp "$HERE/$PROJECT.kicad_pcb" "$HERE/$PROJECT.kicad_sch" "$HERE/$PROJECT.kicad_pro" \
   "$HERE/fp-lib-table" "$HERE/sym-lib-table" "$TMP/"
cp -R "$HERE/libs" "$TMP/libs"
BOARD="$TMP/$PROJECT.kicad_pcb"; SCH="$TMP/$PROJECT.kicad_sch"
kicad() {   # kicad-cli, quiet: its output only on failure, without the harmless Fontconfig cache warnings
  local log="$TMP/kicad-cli.log"
  if ! "$CLI" "$@" >"$log" 2>&1; then grep -v Fontconfig "$log" >&2; return 1; fi
}

rm -rf "$FAB"; mkdir -p "$FAB/$NAME" "$FAB/jlcpcb" "$FAB/reports"

echo "== Checks (zones refilled first, as Plot's \"Check zone fills before plotting\")"
kicad pcb drc --refill-zones --save-board --schematic-parity --severity-all -o "$FAB/reports/drc.rpt" "$BOARD"
kicad pcb drc --schematic-parity --severity-error --format json -o "$TMP/drc.json" "$BOARD"
kicad sch erc --severity-all -o "$FAB/reports/erc.rpt" "$SCH"
kicad sch erc --severity-error --format json -o "$TMP/erc.json" "$SCH"
read -r DRC_ERR UNCONN PARITY ERC_ERR < <("$KPY" - "$TMP/drc.json" "$TMP/erc.json" <<'EOF'
import json, sys
def errors(items):
    return sum(1 for v in items if v.get('severity') == 'error' and not v.get('excluded'))
d = json.load(open(sys.argv[1])); e = json.load(open(sys.argv[2]))
erc = sum(errors(s.get('violations', [])) for s in e.get('sheets', []))
print(errors(d.get('violations', [])), len(d.get('unconnected_items', [])), errors(d.get('schematic_parity', [])), erc)
EOF
)
echo "   DRC errors: $DRC_ERR, unconnected: $UNCONN, schematic mismatches: $PARITY, ERC errors: $ERC_ERR" \
     "(warnings and accepted items in reports/)"

echo "== Gerbers and drill files -> fab_files/$NAME/"
kicad pcb export gerbers --board-plot-params -o "$FAB/$NAME/" "$BOARD"
kicad pcb export drill --format excellon --drill-origin absolute --excellon-units mm \
  --excellon-zeros-format decimal --excellon-oval-format route --excellon-separate-th \
  --generate-map --map-format gerberx2 -o "$FAB/$NAME/" "$BOARD"
(cd "$FAB/$NAME" && zip -q -X -j "$FAB/$NAME.zip" ./*)
echo "   $(ls "$FAB/$NAME" | wc -l | tr -d ' ') files, zipped to fab_files/$NAME.zip"

echo "== Parts lists -> Bill_of_Material.csv, Bill_of_Material_Goup_By_Description.csv"
F='Reference,${QUANTITY},Value,Footprint,Datasheet,Description'; L='Reference,Qty,Value,Footprint,Datasheet,Description'
kicad sch export bom --fields "$F" --labels "$L" --group-by 'Value,Footprint' --ref-range-delimiter '' \
  -o "$HERE/Bill_of_Material.csv" "$SCH"
kicad sch export bom --fields "$F" --labels "$L" --group-by 'Value,Footprint,Description' --ref-range-delimiter '' \
  -o "$HERE/Bill_of_Material_Goup_By_Description.csv" "$SCH"

if [ "$GUI" = 1 ]; then
  echo "== Choose the parts JLCPCB places (window open: click parts, then \"Save and continue\")"
  if "$KPY" "$HERE/jlcpcb_select.py" "$BOARD" "$SELECTION" 2> >(grep -v -E 'Fontconfig|Debug|assert|swig/python|ApplePersistence' >&2); then
    echo "   choice saved in jlcpcb_selection.json"
  else
    echo "   window closed without saving: the previous choice stays"
  fi
fi

echo "== JLCPCB assembly: BOM and CPL -> fab_files/jlcpcb/"
"$KPY" "$HERE/jlcpcb_assembly.py" "$BOARD" "$FAB/jlcpcb" "$NAME" --selection "$SELECTION" $VERIFY \
  2> >(grep -v -E 'Fontconfig|Debug|assert|swig/python' >&2)
LEFT_OUT="$("$KPY" -c "import json,sys; print(', '.join(json.load(open(sys.argv[1])).get('excluded', [])) or 'none')" \
  "$SELECTION" 2>/dev/null || echo none)"

if [ "$STEP" = 1 ]; then
  echo "== 3D model -> generated_imgs_and_3d/$PROJECT.step"
  mkdir -p "$HERE/generated_imgs_and_3d"
  kicad pcb export step --force --drill-origin --subst-models --include-tracks --include-pads --include-zones \
    --include-silkscreen --include-soldermask --min-distance 0.001mm \
    -o "$HERE/generated_imgs_and_3d/$PROJECT.step" "$BOARD"
fi

cat > "$FAB/README.md" <<EOF
# Fabrication files ($(date '+%Y-%m-%d %H:%M'))

Made by \`generate_fab_files.sh\` from the saved KiCad project. Don't edit these files: run the script again.

| Upload to JLCPCB | File |
|---|---|
| The board (Gerbers and drill files) | \`$NAME.zip\` |
| Assembly, back only: BOM | \`jlcpcb/${NAME}_BOM_JLCPCB_back.csv\` |
| Assembly, back only: CPL (pick and place) | \`jlcpcb/${NAME}_CPL_JLCPCB_back.csv\` |
| Assembly, both sides: BOM and CPL | \`jlcpcb/${NAME}_BOM_JLCPCB_both.csv\`, \`jlcpcb/${NAME}_CPL_JLCPCB_both.csv\` |

Taken out of the JLCPCB order (solder them yourself, with the through-hole parts): $LEFT_OUT.

Checks when made: DRC errors $DRC_ERR, unconnected items $UNCONN, schematic mismatches $PARITY, ERC errors $ERC_ERR
(details in \`reports/\`). How to order: \`guides/jlcpcb_assembly.md\`.
EOF
(cd "$FAB" && zip -q -r -X "$FAB/${NAME}_all_files.zip" "$NAME.zip" jlcpcb reports README.md)
echo "== Done: fab_files/ ($NAME.zip to upload, jlcpcb/ for assembly, ${NAME}_all_files.zip with everything)"
if [ "$DRC_ERR" != 0 ] || [ "$UNCONN" != 0 ] || [ "$PARITY" != 0 ] || [ "$ERC_ERR" != 0 ]; then
  echo "WARNING: the checks found problems (see fab_files/reports/). Fix them before ordering."; exit 3
fi
