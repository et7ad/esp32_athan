"""Checks the case's parts against the board: KiCad's 3D model of the assembled board (STEP) plus simple models of
the parts KiCad has none for (the display modules and their headers, the 5-way switch, the screw terminals, the
speaker). Used by enclosure.py --check BOARD_STEP, and by --assembly BOARD_STEP for the assembled clock.

The board is modelled as the case expects it: no header in J6 and a capacitor C1 at most C1['h'] tall. Every
combination of display (OLED, TFT) and USB-C side (front, back) is checked: each case part must not touch the board,
any part on it or the display, and the shells and the knob must not touch each other. Prints every overlap it finds.
"""
import cadquery as cq

import enclosure as E

FRONT_Z = 1.59       # the board's front surface above the bottom of its body, in KiCad's STEP (copper and mask)
BACK_Z = -0.08       # and its back surface


def _inside(bb, part, margin=0.6):
    return (bb.xmin > part['x0'] - margin and bb.xmax < part['x1'] + margin
            and bb.ymin > -part['y1'] - margin and bb.ymax < -part['y0'] + margin)


def board_from_step(path, usbc='front'):
    """KiCad's STEP of the board, moved into the case's frame (Z = 0 at the board's front). Thin layers (copper,
    mask, silkscreen) are left out, and so is a header in J6; C1 is cut to its height. With usbc='back' the USB-C
    receptacle is turned over onto the back."""
    solids = cq.importers.importStep(path).solids().vals()
    body = max(solids, key=lambda v: v.BoundingBox().xlen * v.BoundingBox().ylen)
    b = body.BoundingBox()
    move = cq.Vector(-b.xmin, -b.ymax, -(b.zmin + FRONT_Z))
    keep = [body.translate(move)]
    cap = E.box(E.C1['x0'] - 1, E.C1['x1'] + 1, -E.C1['y1'] - 1, -E.C1['y0'] + 1, -1.0, E.C1['h']).val()
    for v in solids:
        bb = v.BoundingBox()
        if v is body or bb.zlen < 0.1:
            continue
        v = v.translate(move)
        bb = v.BoundingBox()
        if _inside(bb, E.J6) and bb.zmax > 1.0:
            continue
        if _inside(bb, E.C1) and bb.zmax > E.C1['h']:
            v = v.intersect(cap)
        keep.append(v)
    if usbc == 'back':
        j1 = E.J1
        turned = [v for v in keep[1:] if _inside(v.BoundingBox(), j1, 0.5) and v.BoundingBox().zmin > -1.0]
        yc = -(j1['y0'] + j1['y1']) / 2
        zc = (BACK_Z - FRONT_Z) / 2
        keep = [v for v in keep if not any(v is t for t in turned)]
        keep += [t.rotate(cq.Vector(0, yc, zc), cq.Vector(1, yc, zc), 180) for t in turned]
    return keep


def tails_above(display):
    """How far the display header's soldered tails stand above the display's board (solder included): as they come,
    or cut down to what the groove in the face takes."""
    d = E.DISPLAYS[display]
    fits = d['glass_t'] - E.GLASS_SEAT + E.TAIL_GROOVE - 0.1
    return min(E.FEMALE_TAIL - d['pcb_t'] + 0.3, fits), fits


def pin_row(display, z0, z1, side=0.64):
    """A row of square header pins from z0 to z1 at the display's pin row on the board."""
    g = E.display_geometry(display)
    py, n = -g['pins_y'], g['pins']
    span = (n - 1) * 2.54
    out = None
    for i in range(n):
        x = E.DISPLAY_X - span / 2 + i * 2.54
        p = E.box(x - side / 2, x + side / 2, py - side / 2, py + side / 2, z0, z1)
        out = p if out is None else out.union(p)
    return out


def header_body(display, z0, z1):
    g = E.display_geometry(display)
    py, span = -g['pins_y'], (g['pins'] - 1) * 2.54
    return E.box(E.DISPLAY_X - span / 2 - 1.27, E.DISPLAY_X + span / 2 + 1.27, py - 1.27, py + 1.27, z0, z1)


def display_module(display):
    """The display, held by the front shell: glass in the face's pocket, its board behind it with a female header on
    its back; and the male header on the main board that the female one slides onto."""
    d = E.DISPLAYS[display]
    g = E.display_geometry(display)
    zb = E.DISPLAY_Z
    z_pcb = zb + d['pcb_t']
    z_glass = z_pcb + d['glass_t']
    gx, gy, gw, gh = g['glass']
    mx, my, mw, mh = g['module']
    glass = E.box(gx - gw / 2, gx + gw / 2, gy - gh / 2, gy + gh / 2, z_pcb, z_glass)
    pcb = E.box(mx - mw / 2, mx + mw / 2, my - mh / 2, my + mh / 2, zb, z_pcb)
    py = -g['pins_y']
    # parts on the module's back, near its header (a regulator or the backlight transistor)
    parts = E.box(E.DISPLAY_X - 6, E.DISPLAY_X + 6, py - 9, py - 2, zb - d['back_parts'], zb)
    female = header_body(display, zb - E.FEMALE_H, zb)
    tails = pin_row(display, zb - 1.0, z_pcb + tails_above(display)[0])
    male = header_body(display, -E.BOARD_T - E.MALE_PLASTIC, -E.BOARD_T)
    pins = pin_row(display, -E.BOARD_T - E.MALE_PLASTIC, E.MALE_LONG - E.BOARD_T)
    return {f'{display} glass': glass, f'{display} module': pcb.union(parts),
            f'{display} female header': female, f'{display} header tails': tails,
            f'{display} male header': male, f'{display} male pins': pins}


def extras(display, speaker_depth=5.5):
    """Models of the parts KiCad has no 3D model for, in the case's frame: {name: Workplane}."""
    out = {}
    for ref, j in (('J3 speaker terminal', E.J3), ('J5 relay terminal', E.J5)):
        t = E.box(j['x0'], j['x1'], -j['y1'], -j['y0'], 0, j['h'])
        cy = -(j['y0'] + j['y1']) / 2
        for dy in (-2.5, 2.5):                 # screw heads on top, wire entries on the side
            t = t.cut(E.cyl(j['x0'] + 3.9, cy + dy, 1.6, j['h'] - 2.0, j['h'] + 1))
            t = t.cut(E.box(j['x1'] - 4.0, j['x1'] + 1, cy + dy - 1.6, cy + dy + 1.6, 2.0, 5.2))
        out[ref] = t
    sx, sy = E.xy(*E.SWITCH)
    sw = E.box(sx - 5, sx + 5, sy - 5, sy + 5, 0, 3.15)
    sw = sw.union(E.cyl(sx, sy, 3.0, 3.15, E.SWITCH_SHOULDER))
    sw = sw.union(E.box(sx - E.STEM_SQ / 2, sx + E.STEM_SQ / 2, sy - E.STEM_SQ / 2, sy + E.STEM_SQ / 2,
                        E.SWITCH_SHOULDER, E.STEM_TOP))
    out['U2 switch'] = sw
    out.update(display_module(display))
    zf = E.face_z(display)
    cx, cy = E.SPK_C
    r = E.SPK_D / 2
    spk = E.cyl(cx, cy, r, zf - E.SPK_RIM, zf).cut(E.cyl(cx, cy, r - 1.2, zf - E.SPK_RIM + 0.6, zf + 1))
    spk = spk.union(E.cyl(cx, cy, r - 3, zf - speaker_depth, zf - E.SPK_RIM + 0.01))
    out['speaker'] = spk.union(E.cyl(cx, cy, 8.5, zf - speaker_depth - 0.01, zf - 1.5))
    return out


def overlap(a, b):
    try:
        return a.intersect(b).Volume()
    except Exception:  # noqa: BLE001 (an empty or failed boolean: no overlap)
        return 0.0


def apart(a, b):
    ba, bb = a.BoundingBox(), b.BoundingBox()
    return (ba.xmax < bb.xmin or bb.xmax < ba.xmin or ba.ymax < bb.ymin or bb.ymax < ba.ymin
            or ba.zmax < bb.zmin or bb.zmax < ba.zmin)


def summary():
    ins, gap = E.header_insertion()
    print(f'headers: the board\'s pins go {ins:.1f} mm into the display\'s female header, {gap:.1f} mm left under it')
    print(f'closing: the arch posts enter their sockets {E.GUIDE_TOP - E.ZB - E.SEAM_GAP:.1f} mm before the case '
          f'closes, the pins enter the board {-(E.LOCATE_TIP + 1.0):.1f} mm before, the headers meet {ins:.1f} mm '
          f'before')
    for disp in ('tft', 'oled'):
        t, fits = tails_above(disp)
        d = E.DISPLAYS[disp]
        as_is = E.FEMALE_TAIL - d['pcb_t'] + 0.3
        note = 'as they come' if as_is <= fits else f'cut to {fits:.1f} mm (solder included)'
        print(f'{disp.upper()}: case {E.face_z(disp) + E.FACE - E.ZB + E.BACK:.1f} mm thick; the header\'s tails '
              f'{note}')


def run(parts, board_step):
    summary()
    problems = 0
    for disp, usbc in (('oled', 'front'), ('oled', 'back'), ('tft', 'front'), ('tft', 'back')):
        board = board_from_step(board_step, usbc)
        others = [(f'board part at x {s.BoundingBox().xmin:.1f}..{s.BoundingBox().xmax:.1f}, '
                   f'y {-s.BoundingBox().ymax:.1f}..{-s.BoundingBox().ymin:.1f}', s) for s in board]
        others += [(name, wp.val()) for name, wp in extras(disp).items()]
        case = [(name, parts[name].val()) for name in (f'front_{disp}', f'back_usbc_on_{usbc}', f'knob_{disp}')]
        found = []
        for pname, p in case:
            for oname, o in others:
                if not apart(p, o) and overlap(p, o) > 1e-3:
                    found.append(f'{pname} hits {oname}')
        for i, (an, a) in enumerate(case):
            for bn, b in case[i + 1:]:
                if not apart(a, b) and overlap(a, b) > 1e-3:
                    found.append(f'{an} hits {bn}')
        print(f'{disp.upper()}, USB-C on the {usbc}: ' + ('fits' if not found else f'{len(found)} problems'))
        for f in found:
            print('   ', f)
        problems += len(found)
    return problems


COLOURS = {'board': (0.12, 0.42, 0.23), 'board parts': (0.25, 0.25, 0.27), 'front shell': (0.94, 0.91, 0.85),
           'back shell': (0.85, 0.80, 0.71), 'knob': (0.79, 0.64, 0.29), 'terminal': (0.18, 0.68, 0.35),
           'switch': (0.72, 0.72, 0.72), 'glass': (0.04, 0.04, 0.04), 'module': (0.11, 0.25, 0.56),
           'header': (0.09, 0.09, 0.09), 'pins': (0.83, 0.69, 0.23), 'tails': (0.83, 0.69, 0.23),
           'speaker': (0.13, 0.13, 0.13)}


def _colour(name):
    for key, c in COLOURS.items():
        if key in name:
            return cq.Color(*c)
    return cq.Color(0.5, 0.5, 0.5)


def assembly(parts, board_step, display, usbc='back'):
    """The closed clock as one assembly (in the case's frame): board and parts, display and headers, speaker, case."""
    a = cq.Assembly(name=f'athan_v3_{display}')
    board = board_from_step(board_step, usbc)
    a.add(cq.Workplane().add(board[0]), name='board', color=_colour('board'))
    a.add(cq.Workplane().add(cq.Compound.makeCompound(board[1:])), name='board parts', color=_colour('board parts'))
    for name, wp in extras(display).items():
        a.add(wp, name=name.replace(' ', '_'), color=_colour(name))
    a.add(parts[f'back_usbc_on_{usbc}'], name='back_shell', color=_colour('back shell'))
    a.add(parts[f'front_{display}'], name='front_shell', color=_colour('front shell'))
    a.add(parts[f'knob_{display}'], name='knob', color=_colour('knob'))
    return a
