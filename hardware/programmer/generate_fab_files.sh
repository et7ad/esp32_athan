#!/bin/bash
# Makes the programmer's fabrication files from the saved KiCad project, without opening KiCad:
#
#   fab_files/                emptied first, then:
#     athan_prog/             Gerbers, drill file and drill map, Gerber job file
#     athan_prog.zip          the same files: upload this to the board house
#     reports/                DRC and ERC reports
#
# kicad-cli works on a copy of the project, so it never rewrites the project file here.
set -euo pipefail

NAME=athan_prog
KICAD_APP=${KICAD_APP:-/Applications/KiCad/KiCad.app}
CLI="$KICAD_APP/Contents/MacOS/kicad-cli"
HERE="$(cd "$(dirname "$0")" && pwd)"
FAB="$HERE/fab_files"
[ -x "$CLI" ] || { echo "kicad-cli not found at $CLI (set KICAD_APP)"; exit 1; }

TMP="$(mktemp -d -t progfab)"
trap 'rm -rf "$TMP"' EXIT
cp "$HERE/$NAME.kicad_pcb" "$HERE/$NAME.kicad_sch" "$HERE/$NAME.kicad_pro" "$TMP/"
cp -R "$HERE/fp-lib-table" "$HERE/$NAME.pretty" "$TMP/"
kicad() {   # kicad-cli, quiet: its output only on failure
  local log="$TMP/kicad-cli.log"
  if ! "$CLI" "$@" >"$log" 2>&1; then grep -v Fontconfig "$log" >&2; return 1; fi
}

rm -rf "$FAB"; mkdir -p "$FAB/$NAME" "$FAB/reports"
echo "== Checks"
kicad pcb drc --schematic-parity --severity-all --exit-code-violations -o "$FAB/reports/drc.rpt" "$TMP/$NAME.kicad_pcb" \
  || { echo "   DRC found problems: fab_files/reports/drc.rpt"; exit 3; }
kicad sch erc --severity-all --exit-code-violations -o "$FAB/reports/erc.rpt" "$TMP/$NAME.kicad_sch" \
  || { echo "   ERC found problems: fab_files/reports/erc.rpt"; exit 3; }
echo "   DRC and ERC clean"

echo "== Gerbers and drill files -> fab_files/$NAME/"
kicad pcb export gerbers --layers F.Cu,B.Cu,F.Mask,B.Mask,F.Silkscreen,B.Silkscreen,Edge.Cuts \
  --subtract-soldermask --no-protel-ext -o "$FAB/$NAME/" "$TMP/$NAME.kicad_pcb"
kicad pcb export drill --format excellon --drill-origin absolute --excellon-units mm \
  --excellon-zeros-format decimal --excellon-oval-format route --excellon-separate-th \
  --generate-map --map-format gerberx2 -o "$FAB/$NAME/" "$TMP/$NAME.kicad_pcb"
(cd "$FAB/$NAME" && zip -q -X -j "$FAB/$NAME.zip" ./*)
echo "== Done: fab_files/$NAME.zip ($(ls "$FAB/$NAME" | wc -l | tr -d ' ') files): a 2-layer board, 1.6 mm, 16.5 x 6.6 mm"
