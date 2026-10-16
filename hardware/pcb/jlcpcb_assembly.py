"""JLCPCB assembly files from the board: the BOM and the CPL (component placement list, "pick and place").

Run it with KiCad's own Python, which has KiCad's pcbnew module (generate_fab_files.sh does):
    <KiCad python> jlcpcb_assembly.py board.kicad_pcb out_dir prefix [--selection jlcpcb_selection.json] [--verify]

Which parts: surface-mount footprints with an LCSC field that are not marked DNP, minus the parts taken out in
jlcpcb_select.py (the "excluded" list of the selection file). Through-hole parts, the displays, the solder jumpers,
the holes and the QR codes are soldered (or not soldered) by hand.

Each placement, as JLCPCB expects it:
- position: the centre of the part's soldered pads, in the same coordinates as the Gerbers (y up);
- rotation: KiCad's angle; on the bottom side 180 minus that angle (JLCPCB looks at the bottom from below); plus a
  correction for parts whose footprint in JLCPCB's library is turned against KiCad's (ROTATION below).

Files written, for the two ways of ordering:
    <prefix>_BOM_JLCPCB_back.csv, <prefix>_CPL_JLCPCB_back.csv    JLCPCB assembles the back only
    <prefix>_BOM_JLCPCB_both.csv, <prefix>_CPL_JLCPCB_both.csv    JLCPCB assembles both sides

--verify downloads each part's footprint from JLCPCB's library (EasyEDA, with curl) and checks pad by pad that it
lands on the KiCad footprint at the written position and rotation. Run it after choosing a new part.
"""
import csv, json, math, os, re, subprocess, sys, tempfile
import pcbnew

# JLCPCB part -> rotation (degrees, counter-clockwise) added to KiCad's angle, where the part's footprint in
# JLCPCB's library is turned against the KiCad footprint. Found by fitting both footprints pad by pad (--verify).
ROTATION = {
    'C6186': 180,     # AMS1117-3.3, SOT-223: pin 1 at the opposite corner
    'C910544': 270,   # MAX98357AETE+T, TQFN-16
}
# JLCPCB part -> {KiCad pad: JLCPCB pad} where the two number a pin differently. Used only by --verify.
# (Some LEDs number the cathode 2 in JLCPCB's footprint; KiCad's LED footprints number it 1.)
PADS = {
}


def mm(v):
    return pcbnew.ToMM(v)


def natural(ref):
    m = re.match(r'([A-Za-z]+)(\d+)', ref)
    return (m.group(1), int(m.group(2))) if m else (ref, 0)


def ascii_value(fp):
    return fp.GetValue().replace('\u00b5', 'u').replace('\u03bc', 'u')   # 22uF, not 22µF, in the uploaded files


def soldered_pads(fp):
    return [p for p in fp.Pads() if p.GetAttribute() != pcbnew.PAD_ATTRIB_NPTH and p.IsOnCopperLayer()]


def placement(board, fp, origin):
    """(x, y, rotation, side) of one part for JLCPCB: pad-centre position, y up, corrected rotation."""
    pads = soldered_pads(fp)
    bb = pads[0].GetBoundingBox()
    for p in pads[1:]:
        bb.Merge(p.GetBoundingBox())
    c = bb.GetCenter()
    bottom = fp.IsFlipped()
    rot = fp.GetOrientationDegrees()
    rot = (180 - rot) % 360 if bottom else rot % 360
    rot = (rot + ROTATION.get(fp.GetFieldText('LCSC'), 0)) % 360
    return mm(c.x - origin.x), -mm(c.y - origin.y), rot, 'bottom' if bottom else 'top'


def parts(board, excluded=()):
    """Footprints JLCPCB places, with a warning for surface-mount parts left without a part number."""
    out = []
    for fp in board.GetFootprints():
        if not (fp.GetAttributes() & pcbnew.FP_SMD) or fp.IsDNP() or fp.GetReference() in excluded:
            continue
        if fp.GetAttributes() & pcbnew.FP_EXCLUDE_FROM_POS_FILES:
            continue
        lcsc = fp.GetFieldText('LCSC') if fp.HasField('LCSC') else ''
        if not lcsc:
            print(f'  warning: {fp.GetReference()} ({fp.GetValue()}) is surface-mount but has no LCSC number: not placed')
            continue
        out.append(fp)
    return sorted(out, key=lambda f: natural(f.GetReference()))


def write(board, fps, out_dir, prefix, tag):
    plot = board.GetPlotOptions()
    origin = board.GetDesignSettings().GetAuxOrigin() if plot.GetUseAuxOrigin() else pcbnew.VECTOR2I(0, 0)
    groups = {}
    with open(os.path.join(out_dir, f'{prefix}_CPL_JLCPCB_{tag}.csv'), 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(['Designator', 'Val', 'Package', 'Mid X', 'Mid Y', 'Rotation', 'Layer'])
        for fp in fps:
            x, y, rot, side = placement(board, fp, origin)
            pkg = str(fp.GetFPID().GetLibItemName())
            w.writerow([fp.GetReference(), ascii_value(fp), pkg, f'{x:.6f}', f'{y:.6f}', f'{rot:g}', side])
            key = (ascii_value(fp), pkg, fp.GetFieldText('LCSC'))
            groups.setdefault(key, []).append(fp.GetReference())
    with open(os.path.join(out_dir, f'{prefix}_BOM_JLCPCB_{tag}.csv'), 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(['Comment', 'Designator', 'Footprint', 'LCSC Part #'])
        for (value, pkg, lcsc), refs in sorted(groups.items(), key=lambda kv: natural(kv[1][0])):
            w.writerow([value, ','.join(refs), pkg, lcsc])
    return len(fps), len(groups)


# ---------- --verify: JLCPCB's own footprint against the KiCad pads ----------

def easyeda(code, cache):
    path = os.path.join(cache, f'{code}.json')
    if not os.path.exists(path):
        subprocess.run(['curl', '-s', '-m', '30', '-o', path,
                        f'https://easyeda.com/api/products/{code}/components?version=6.4.19.5'], check=True)
    ds = json.load(open(path))['result']['packageDetail']['dataStr']
    hx, hy = float(ds['head']['x']), float(ds['head']['y'])
    pads = {}
    for s in ds['shape']:
        if s.startswith('PAD~'):
            f = s.split('~')
            pads.setdefault(f[8], []).append(((float(f[2]) - hx) * 0.254, (float(f[3]) - hy) * 0.254))
    return pads


def verify(board, fps):
    """Put JLCPCB's footprint where the CPL says (its origin at the written position, turned by the written
    rotation, the bottom seen from below) and measure how far each numbered pad lands from KiCad's pad."""
    cache = tempfile.mkdtemp(prefix='jlcpcb_fp_')
    origin = pcbnew.VECTOR2I(0, 0)
    worst = 0.0
    for fp in fps:
        code = fp.GetFieldText('LCSC')
        try:
            ep = easyeda(code, cache)
        except Exception as e:  # noqa: BLE001
            print(f'  {fp.GetReference():4s} {code}: no footprint from JLCPCB ({e})'); continue
        x, y, rot, side = placement(board, fp, origin)
        a = math.radians(rot); pm = PADS.get(code, {})
        errs = []
        for p in soldered_pads(fp):
            n = pm.get(p.GetNumber(), p.GetNumber())
            if n not in ep or len(ep[n]) != 1 or sum(q.GetNumber() == p.GetNumber() for q in fp.Pads()) != 1:
                continue
            ex, ey = ep[n][0]
            vx, vy = ex * math.cos(a) + ey * math.sin(a), -ex * math.sin(a) + ey * math.cos(a)   # y down, CCW
            bx, by = mm(p.GetPosition().x), mm(p.GetPosition().y)
            if side == 'bottom':
                vx = -vx        # seen from below
            errs.append(math.hypot(x + vx - bx, -y + vy - by))
        if not errs:
            print(f'  {fp.GetReference():4s} {code}: no pad numbers in common, check it in JLCPCB\'s preview'); continue
        e = max(errs); worst = max(worst, e)
        flag = 'ok' if e < 0.5 else 'CHECK: footprint or rotation does not match'
        print(f'  {fp.GetReference():4s} {code:9s} {side:6s} rotation {rot:5g}  pads off by at most {e:.3f} mm '
              f'({len(errs)} pads)  {flag}')
    return worst


if __name__ == '__main__':
    board_file, out_dir, prefix = sys.argv[1:4]
    excluded = []
    if '--selection' in sys.argv:
        sel = sys.argv[sys.argv.index('--selection') + 1]
        if os.path.exists(sel):
            excluded = json.load(open(sel)).get('excluded', [])
    board = pcbnew.LoadBoard(board_file)
    fps = parts(board, set(excluded))
    if excluded:
        print(f'  taken out of the JLCPCB order (you solder them): {", ".join(excluded)}')
    back = [f for f in fps if f.IsFlipped()]
    os.makedirs(out_dir, exist_ok=True)
    n, g = write(board, back, out_dir, prefix, 'back')
    print(f'  back only:  {n} parts, {g} BOM lines')
    n, g = write(board, fps, out_dir, prefix, 'both')
    print(f'  both sides: {n} parts, {g} BOM lines')
    if '--verify' in sys.argv:
        print('  checking every part against its footprint in JLCPCB\'s library:')
        if verify(board, fps) >= 0.5:
            sys.exit(2)
