#!/usr/bin/env python3
"""The 3D-printed case for the Athan clock V3 board (hardware/pcb): a mihrab, a rectangle with a pointed arch.

Two shells and a knob:

    back shell    the back and most of the walls. The board clicks into it on its own: its bottom edge slides under
                  two ledges, its top edge snaps under the lips of a spring along it, and four ribs keep it centred.
                  Openings for the USB-C and the relay wires; sockets for the front shell's two long posts.
    front shell   the face (display window, joystick hole, holes over the relay terminal's screws, LED, speaker
                  grille, crescent) and the front of the walls. The display clicks into it: its glass sits in a
                  pocket behind the window, its board's bottom edge in two grooved stops, its sides under the lips
                  of two springs. The front shell clicks onto the back, and as it closes the female header on the
                  display's back slides onto a male header on the board. Two long posts entering their sockets, then
                  two pins entering the board's spare display holes, line the headers up first.
    knob          pressed onto the 5-way switch's stem; its cap passes through the face's hole, so the front comes
                  off and goes on over it

Six M3 x 16 countersunk screws from the back add hold: four through the board's corner holes (they clamp the board
between the shells) and two in the arch.

Variants: the front shell and the knob for the OLED (1.3", DS1) or the TFT (1.54", DS2), whose glass sits higher;
the back shell for the USB-C J1 soldered on the front or on the back of the board. The case has no room for the
barrel jack J2 (a fallback for a board without a working USB-C, used without the case) or for a header in J6 (the
programmer presses into its holes).

Run it with CadQuery (Python 3.10 to 3.12; pip install cadquery arabic-reshaper python-bidi):
    python enclosure.py                        STL (to print) and STEP (to open in CAD) of every part and variant
    python enclosure.py --check board.step     also checks every part against the board's 3D model
                                               (hardware/pcb/generated_imgs_and_3d/esp32_athan.step, made by
                                               generate_fab_files.sh)
    python enclosure.py --assembly board.step  also writes step/assembly_tft.step and assembly_oled.step: the
                                               closed clock with the board, its parts, the display and the headers

Coordinates: X to the right and Y up, as you look at the clock; Z toward you, 0 at the board's front surface.
Board positions are written as KiCad shows them (x right, y down, from the board's top-left corner) and turned
into the case's frame by xy(). The STL files are turned for printing; the STEP files sit where the board's STEP
puts the board, so the two open together.
"""
import argparse
import math
import os

import cadquery as cq

HERE = os.path.dirname(os.path.abspath(__file__))


def xy(bx, by):
    """KiCad board position -> case X, Y."""
    return bx, -by


# ---------------------------------------------------------------- the board (hardware/pcb/esp32_athan.kicad_pcb)

BOARD_W, BOARD_H, BOARD_T = 59.6, 50.0, 1.6
HOLES = [(3.5, 3.5), (56.1, 3.5), (3.5, 46.5), (56.1, 46.4)]          # H1..H4, M3 (3.2 mm)
TFT_HOLES = [(16.15, 3.2), (43.45, 3.2), (16.15, 42.0), (43.45, 42.0)]  # DS2's 2.2 mm holes
OLED_HOLES = [(14.05, 17.7), (45.55, 17.7), (14.05, 47.4), (45.55, 47.4)]  # DS1's 3.0 mm holes
DISPLAY_X = 29.8                    # both display headers are centred on this line, and so is the case
OLED_PINS_Y, TFT_PINS_Y = 17.0, 2.2  # header pin rows (4 and 8 pins, 2.54 mm)
LED = (10.4, 4.0)                   # D1, 1206, 1.1 mm tall
SWITCH = (53.0, 25.75)              # U2's stem (pads 47.85..58.15 x 22.5..29.0, stem in the middle)
STEM_TOP, STEM_SQ, SWITCH_SHOULDER = 10.0, 3.2, 5.8   # 5-way switch (SKQUCAA010 type): stem top, stem, round base
# Parts at the board's edges and the tallest ones under the face (KiCad fab outlines; heights from the parts)
J1 = dict(x0=-0.65, x1=6.25, y0=20.08, y1=29.12, h=3.27)   # USB-C, front or back
J3 = dict(x0=51.19, x1=59.09, y0=7.85, y1=18.45, h=10.3)   # speaker terminal, wires enter from the right
J5 = dict(x0=51.19, x1=59.09, y0=31.55, y1=42.15, h=10.3)  # relay terminal, its wires leave the case
J5_SCREWS = [(55.24, 34.10), (55.24, 39.10)]               # its screws, over its pins: reached through the face
C1 = dict(x0=0.9, x1=9.2, y0=31.75, y1=41.25, h=10.5)      # 470 uF capacitor, at most 10.5 mm tall with its base
J6 = dict(x0=29.93, x1=45.17, y0=8.38, y1=10.92)           # programming holes: no header (the programmer presses in)

# The displays, measured from the header's pin row (dy down) and the board's centre line (x). The OLED follows
# LCDWiki's drawing of the MC130GX 1.3" I2C OLED. The TFT is a 1.54" ST7789 module as fitted in the window test:
# 42.2 mm long and 4.65 mm from its glass front to its board's back (LCDWiki's MSP1541 drawing: 43.72 and 3.6).
# Change these if your module differs; the window test shows it in a few minutes.
DISPLAYS = {
    'oled': dict(pins_y=OLED_PINS_Y, pins=4,
                 module=(35.4, 33.5), module_dy=-2.0,          # PCB, its top edge 2.0 above the pins
                 glass=(34.5, 23.0), glass_dy=3.25,            # glass, its top edge 3.25 below the pins
                 active=(29.42, 14.70), active_dy=5.35,        # visible area
                 pcb_t=1.2, glass_t=1.4, back_parts=1.0),
    'tft': dict(pins_y=TFT_PINS_Y, pins=8,
                module=(32.0, 42.2), module_dy=-1.5,
                glass=(31.52, 35.10), glass_dy=2.81,
                active=(27.72, 27.72), active_dy=4.95,
                pcb_t=1.6, glass_t=3.05, back_parts=1.2),      # glass_t: the whole panel in front of the board
}

# The display plugs into the board. A standard 2.54 mm male header goes into the board from the back (its plastic
# under the board, its short ends cut off), so its long pins stand MALE_LONG - BOARD_T above the board; a standard
# female header on the display's back slides onto them. The display's board then sits DISPLAY_Z above the main board
# and the female header stops short of it, so the case closes on its own seat, never on the headers.
DISPLAY_Z = 9.3                     # the display board's back above the main board's front
FEMALE_H, FEMALE_TAIL = 8.5, 3.0    # standard female header: body, solder tails below it
MALE_PLASTIC, MALE_LONG = 2.5, 6.0  # standard male header: plastic, long pins above it
TAIL_GROOVE = 1.3                   # groove inside the face over the female header's soldered tails

# ---------------------------------------------------------------- the case

WALL = 2.0            # side walls
FACE = 2.0            # front face
BACK = 2.0            # back plate
ZS = 6.0              # where the two shells meet
SEAM_GAP = 0.15       # left open at the seam so that the screws clamp the board, not the shells
ZB = -6.0             # inside of the back plate (board back -1.6, ESP32 module -4.7, room for a USB-C plug)
TERMINAL_ROOM = 3.0   # between the terminals' wire entries and the right wall
X_R = J3['x1'] + TERMINAL_ROOM + WALL  # outside of the right wall
X_L = 2 * DISPLAY_X - X_R             # the case is symmetric about the displays' centre line
Y_BOT = -(BOARD_H + 0.6 + WALL)       # outside of the bottom wall
R_ARCH = 52.0         # radius of the arch's two arcs (outside); each is centred on the other side's springing line
CORNER_R = 4.0        # bottom corners
EDGE_R = 1.5          # rounding of the front and back edges
TONGUE, TONGUE_H = (1.1, 2.1), 1.5   # the back shell's tongue (offsets from the outside) and how far it rises

GLASS_GAP = 0.2       # around the display glass in its pocket
GLASS_SEAT = 1.0      # the glass sits this far into the face
WINDOW_MARGIN = 0.5   # window beyond the visible area, each side
WINDOW_BEVEL = 0.6

SPK_D, SPK_RIM = 40.0, 2.0            # speaker: diameter, thickness of its outer rim (what the clips hold)
SPK_C = (DISPLAY_X, 22.0)             # its centre (case X, Y), above the board's top edge

SCREW_PILOT, SCREW_CLEAR, CSK_D = 2.7, 3.4, 6.4     # M3 self-tapping into PLA, clearance, countersink
POST_D, TUBE_D = 6.0, 6.5
ARCH_SCREWS = [(2.0, 10.0), (2 * DISPLAY_X - 2.0, 10.0)]   # case X, Y: the arch's lower corners

KNOB_D, KNOB_HEAD_D, KNOB_HOLE_D = 8.0, 9.4, 10.0   # shaft, cap, hole in the face: the cap passes through the hole,
                                                    # so the front comes off and goes on over the knob
KNOB_FIT = 0.25      # the knob's square hole is this much wider than the stem: a press fit that holds
# The board clicks into the back shell: its bottom edge slides under two ledges, its top edge snaps under the two
# lips of a spring along it, a wall held by a post at each end whose middle bends outward
BOARD_PLAY = 0.15                     # above the board's front surface, under the ledges' and the lips' undersides
HOOK_SPAN = (12.0, 2 * DISPLAY_X - 12.0)       # the wall's ends (case X), posts included
HOOK_T, HOOK_POST, HOOK_POST_T = (1.3, 1.1), 3.0, 1.0   # the wall (root, top: tapered); posts: length, extra thickness
HOOKS_X, HOOK_W, HOOK_LIP = (DISPLAY_X - 6.3, DISPLAY_X + 6.3), 7.0, 0.5   # the lips: centres, width, over the board
HOOK_ROOT, HOOK_TOP = 1.4, 3.0                # how deep the wall starts in the back plate; its top, above the
                                              # board (the lips' ramps run up to it, and it is where to push it back)
LEDGES_X = ((DISPLAY_X - 9.5, DISPLAY_X - 2.5), (DISPLAY_X + 2.5, DISPLAY_X + 9.5))   # bottom edge, clear of posts
LEDGE_LIP = 0.7
RIBS_Y, RIB_T, RIB_GAP = (-4.0, -44.0), 1.2, 0.2   # side ribs that keep the board centred (case Y), clear of the
                                                   # USB-C and the terminals
# The front shell clicks onto the back: rounded beads on the tongue, grooves in the front shell's rim
DETENT_R, DETENT_GROOVE_R, DETENT_OFFSET, DETENT_Z, DETENT_L = 0.45, 0.5, 1.15, 6.8, 6.0
# The display clicks into the front shell. Its glass sits in a pocket behind the window and its board's bottom edge
# goes into grooves in two stops; pressed in, the board's sides pass the lips of two springs, walls on the face
# that bend outward and snap back over it. A stopper stands over its top edge. All of it is placed from the
# display's board (its edges, its back at DISPLAY_Z) and the face.
SPRING_GAP, SPRING_T, SPRING_L = 0.4, 1.4, 24.0     # gap to the board's side, the wall, its length between the posts
SPRING_POST, SPRING_POST_T = 3.0, 0.75              # a post at each end of the wall, thicker outward by this
SPRING_LIP, SPRING_LIP_L, SPRING_DEPTH = 0.5, 8.0, 2.05   # lip over the board, its length; wall below the board's back
STOP_W, STOP_OUT, STOP_FACE, STOP_GROOVE = 8.0, 2.25, 0.5, 0.8   # bottom stops: width, past the board's side, face
                                                                 # above its bottom edge, groove into the face
STOP_DEPTH = 1.95     # the stops and the top stopper reach this far below the display board's back
TOP_GAP, TOP_W = 1.3, 17.2   # the top stopper: above the board's top edge, its width
# Closing the case: the front's two arch posts enter sockets in the back, then two pins enter the board's spare
# display holes, before the display's header meets the board's
GUIDE_TOP, GUIDE_CLEAR, GUIDE_WALL = 3.0, 0.15, 1.3
LOCATE_TIP = -5.5     # the pins' tips, through the board
FACE_TEXT = 'حي على الصلاة'   # engraved under the display ("come to prayer", from the athan); '' for none
FACE_TEXT_FONT, FACE_TEXT_SIZE = 'Arial', 8.4   # a font with Arabic presentation forms (Arial, Tahoma, Geeza Pro)
FACE_TEXT_DEPTH = 1.2  # into the 2 mm face: deep enough to read by its shadows
TERMINAL_HOLE_D = 4.4  # holes in the face over J5's screws
STEP_ORIGIN = (100.0, -100.0, 1.59)  # where the board's STEP (from KiCad) puts the case's origin
LIGHT_PIPE_D = 2.0    # hole for a light pipe (a piece of 1.75 mm clear filament) over the LED


def face_z(display):
    """The inside of the face for a display: its glass sits GLASS_SEAT into the face."""
    d = DISPLAYS[display]
    return DISPLAY_Z + d['pcb_t'] + d['glass_t'] - GLASS_SEAT


def header_insertion():
    """How far the board's pins go into the display's female header, and the gap left under the header."""
    gap = DISPLAY_Z - FEMALE_H
    return MALE_LONG - BOARD_T - gap, gap


def outline(d=0.0):
    """The case's outline offset inward by d, as a closed wire at Z = 0: a rectangle with a pointed arch on top.
    The arch springs from the board's top edge (Y = 0) with vertical sides."""
    xl, xr, yb, r = X_L + d, X_R - d, Y_BOT + d, R_ARCH - d
    cl, cr = X_L + R_ARCH, X_R - R_ARCH          # centres of the arcs through the left and right springing points
    apex = math.sqrt(r * r - (DISPLAY_X - cr) ** 2)
    rb = max(CORNER_R - d, 0.5)
    k = 1 - math.sqrt(0.5)
    left = lambda y: (cl - math.sqrt(r * r - y * y), y)
    right = lambda y: (cr + math.sqrt(r * r - y * y), y)
    return (cq.Workplane('XY').moveTo(xl, 0).lineTo(xl, yb + rb)
            .threePointArc((xl + rb * k, yb + rb * k), (xl + rb, yb))
            .lineTo(xr - rb, yb)
            .threePointArc((xr - rb * k, yb + rb * k), (xr, yb + rb))
            .lineTo(xr, 0)
            .threePointArc(right(apex / 2), (DISPLAY_X, apex))
            .threePointArc(left(apex / 2), (xl, 0))
            .close())


def apex_y(d=0.0):
    cr = X_R - R_ARCH
    return math.sqrt((R_ARCH - d) ** 2 - (DISPLAY_X - cr) ** 2)


def slab(d, z0, z1):
    return outline(d).extrude(z1 - z0).translate((0, 0, z0))


def cyl(x, y, r, z0, z1):
    return cq.Workplane('XY').circle(r).extrude(z1 - z0).translate((x, y, z0))


def cone(x, y, r0, r1, z0, z1):
    """A truncated cone from radius r0 at z0 to r1 at z1."""
    return cq.Workplane('XY').add(cq.Solid.makeCone(r0, r1, z1 - z0, cq.Vector(x, y, z0), cq.Vector(0, 0, 1)))


def box(x0, x1, y0, y1, z0, z1):
    return cq.Workplane('XY').box(x1 - x0, y1 - y0, z1 - z0, centered=False).translate((x0, y0, z0))


def rounded_box(cx, cy, w, h, r, z0, z1):
    return (cq.Workplane('XY').rect(w, h).extrude(z1 - z0).edges('|Z').fillet(r)
            .translate((cx, cy, z0)))


def star_points(cx, cy, r, turn=0.0):
    """An eight-pointed star (two squares, the rub el hizb), circumradius r."""
    ri = r * math.cos(math.pi / 4) / math.cos(math.pi / 8)
    pts = []
    for i in range(16):
        a = turn + i * math.pi / 8
        rr = r if i % 2 == 0 else ri
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    return pts


def stars(centres, z0, z1):
    wp = cq.Workplane('XY')
    for cx, cy, r in centres:
        wp = wp.polyline(star_points(cx, cy, r, math.pi / 8)).close()
    return wp.extrude(z1 - z0).translate((0, 0, z0))


def display_geometry(name):
    """Case X, Y boxes (cx, cy, w, h) of a display's module, glass and visible area, and its pin row."""
    d = DISPLAYS[name]
    py = d['pins_y']
    def rect(size, dy):
        w, h = size
        return DISPLAY_X, -(py + dy + h / 2), w, h
    return dict(module=rect(d['module'], d['module_dy']), glass=rect(d['glass'], d['glass_dy']),
                active=rect(d['active'], d['active_dy']), pins_y=py, pins=d['pins'])


# ---------------------------------------------------------------- front shell

def grille():
    """Star holes over the speaker's cone: one in the middle and rings of 8 and 16 (about 24 % open)."""
    cx, cy = SPK_C
    centres = [(cx, cy, 3.4)]
    for n, rad, r in ((8, 7.6, 2.1), (16, 13.4, 1.65)):
        for i in range(n):
            a = 2 * math.pi * i / n + (math.pi / n if n == 16 else 0)
            centres.append((cx + rad * math.cos(a), cy + rad * math.sin(a), r))
    return centres


def speaker_mount(zf):
    """A ring around the speaker and three snap clips that hold its rim against the face."""
    cx, cy = SPK_C
    ri = SPK_D / 2 + 0.2
    ring_h = SPK_RIM + 0.6
    ring = (cq.Workplane('XY').circle(ri + 1.2).circle(ri).extrude(ring_h).translate((cx, cy, zf - ring_h)))
    clip_h = SPK_RIM + 2.2
    parts = ring
    slits = None
    for a_deg in (90, 210, 330):
        finger = (cq.Workplane('XY').box(1.2, 5.0, clip_h, centered=(False, True, False))
                  .translate((ri, 0, zf - clip_h)))
        lip = (cq.Workplane('XZ').polyline([(ri + 0.01, zf - SPK_RIM), (ri - 0.8, zf - SPK_RIM),
                                            (ri - 0.8, zf - SPK_RIM - 0.4), (ri + 0.01, zf - clip_h)]).close()
               .extrude(2.5, both=True))
        clip = finger.union(lip).rotate((0, 0, 0), (0, 0, 1), a_deg).translate((cx, cy, 0))
        parts = parts.union(clip)
        cut = (cq.Workplane('XY').box(3.0, 0.6, ring_h + 0.2, centered=(False, True, False))
               .translate((ri - 0.9, 0, zf - ring_h - 0.1)))
        for side in (-2.8, 2.8):
            s = cut.translate((0, side, 0)).rotate((0, 0, 0), (0, 0, 1), a_deg).translate((cx, cy, 0))
            slits = s if slits is None else slits.union(s)
    return parts.cut(slits)


def display_mount(display):
    """What holds the display in the front shell: two springs at its board's sides, two grooved stops under its
    bottom edge and a stopper over its top edge (the speaker ring's lowest stretch, where that is the place)."""
    d = DISPLAYS[display]
    zf = face_z(display)
    g = display_geometry(display)
    mx, my, mw, mh = g['module']
    gy = g['glass'][1]                                # the springs are centred on the glass
    top, bottom = my + mh / 2, my - mh / 2
    zb = DISPLAY_Z                                    # the display board's back
    z_spring, z_stop = zb - SPRING_DEPTH, zb - STOP_DEPTH
    kx, ky = xy(*SWITCH)
    keep = KNOB_HOLE_D / 2 + 0.3                      # the knob's room to tilt
    parts = []
    for side in (1, -1):                              # right, left
        edge = mx + side * mw / 2
        gx = lambda u: edge + side * u                # u: outward from the board's side
        span = (gy - SPRING_L / 2 - SPRING_POST, gy + SPRING_L / 2 + SPRING_POST)
        near = min(abs(kx - gx(SPRING_GAP)), abs(kx - gx(SPRING_GAP + SPRING_T)))
        if near < keep:                               # the knob's way: the spring moves below it, shorter
            u_far = SPRING_GAP + SPRING_T + SPRING_POST_T
            dy = math.sqrt(keep ** 2 - min(abs(kx - gx(u_far)), near) ** 2) + 0.5
            span = (bottom + 1.0, min(span[1], ky - dy))
        y0, y1 = span
        free_mid = (y0 + y1) / 2
        x0, x1 = sorted((gx(SPRING_GAP), gx(SPRING_GAP + SPRING_T)))
        wall = box(x0, x1, y0, y1, z_spring, zf + 0.01)
        px0, px1 = sorted((gx(SPRING_GAP), gx(SPRING_GAP + SPRING_T + SPRING_POST_T)))
        wall = wall.union(box(px0, px1, y0, y0 + SPRING_POST, z_spring, zf + 0.01))
        wall = wall.union(box(px0, px1, y1 - SPRING_POST, y1, z_spring, zf + 0.01))
        # the lip: catches the board's back, a 45 degree lead-in under it
        lip = (cq.Workplane('XZ').polyline([(gx(SPRING_GAP + 0.01), zb), (gx(-SPRING_LIP), zb),
                                            (gx(-SPRING_LIP), z_spring + 0.7), (gx(0.2), z_spring),
                                            (gx(SPRING_GAP + 0.01), z_spring)]).close()
               .extrude(SPRING_LIP_L / 2, both=True).translate((0, free_mid, 0)))
        parts.append(wall.union(lip))
        # the bottom stop on this side, with the groove the board's bottom edge goes into
        s0, s1 = sorted((gx(STOP_OUT), gx(STOP_OUT - STOP_W)))
        y_face = bottom + STOP_FACE
        y_far = max(Y_BOT + WALL - 0.01, y_face - 5.02)
        stop = (cq.Workplane('XY').box(s1 - s0, y_face - y_far, zf + 0.01 - z_stop, centered=False)
                .edges('|Z and >Y').fillet(1.0).translate((s0, y_far, z_stop)))
        stop = stop.cut(box(s0 - 0.1, s1 + 0.1, y_face - STOP_GROOVE, y_face + 0.1, zb - 0.05, zb + d['pcb_t'] + 0.15))
        parts.append(stop)
    # the top stopper: the speaker ring's lowest stretch, carried down, when it passes just above the board;
    # otherwise a wall
    cx, cy = SPK_C
    ro = SPK_D / 2 + 0.2 + 1.2
    if abs((cy - ro) - (top + TOP_GAP)) < 0.5:
        ring = cq.Workplane('XY').circle(ro).circle(ro - 1.2).extrude(zf + 0.01 - z_stop).translate((cx, cy, z_stop))
        parts.append(ring.intersect(box(cx - ro, cx + ro, cy - ro - 1, top + TOP_GAP + 1.8, z_stop - 1, zf + 1)))
    else:
        parts.append(box(DISPLAY_X - TOP_W / 2, DISPLAY_X + TOP_W / 2, top + TOP_GAP, top + TOP_GAP + 1.2,
                         z_stop, zf + 0.01))
    out = parts[0]
    for p in parts[1:]:
        out = out.union(p)
    return out


def crescent(zf):
    """The finial on the arch's tip: a short stem, a bead and a crescent opening upward."""
    ya = apex_y(0)
    z0, z1 = zf - 2.0, zf + FACE
    cx = DISPLAY_X
    stem = box(cx - 1.1, cx + 1.1, ya - 2.0, ya + 3.4, z0, z1)
    bead = cyl(cx, ya + 3.8, 2.0, z0, z1)
    moon = cyl(cx, ya + 10.6, 5.0, z0, z1).cut(cyl(cx, ya + 12.2, 4.6, z0 - 1, z1 + 1))
    return stem.union(bead).union(moon)


def face_text(display):
    """The Arabic text engraved under the display, its letters centred between the window and the engraved line."""
    if not FACE_TEXT:
        return None
    try:
        import arabic_reshaper
        from bidi.algorithm import get_display
    except ImportError:
        print('note: the face text needs "pip install arabic-reshaper python-bidi"; left out')
        return None
    visual = get_display(arabic_reshaper.reshape(FACE_TEXT))     # joined letter forms, in drawing order
    g = display_geometry(display)
    _, ay, _, ah = g['active']
    top = ay - ah / 2 - WINDOW_MARGIN - WINDOW_BEVEL
    y = (top + Y_BOT + 3.8) / 2
    plane = cq.Plane(origin=(DISPLAY_X, y, face_z(display) + FACE), xDir=(1, 0, 0), normal=(0, 0, 1))
    glyphs = cq.Workplane(plane).text(visual, FACE_TEXT_SIZE, -FACE_TEXT_DEPTH, font=FACE_TEXT_FONT, kind='bold',
                                      halign='center', valign='center', combine=False).solids().vals()
    text = glyphs[0]
    for g_ in glyphs[1:]:            # joined letters overlap: one solid, or the cut goes wrong
        text = text.fuse(g_)
    bb = text.BoundingBox()          # the letters' own middle, not the font's line box
    return cq.Workplane('XY').newObject([text.clean().translate(cq.Vector(0, y - (bb.ymin + bb.ymax) / 2, 0))])


def front_shell(display, text=True):
    """The face and the front of the walls, with everything that hangs from the face inside."""
    zf = face_z(display)
    z_out = zf + FACE
    shell = slab(0, ZS, z_out)
    try:
        shell = shell.faces('>Z').edges().fillet(EDGE_R)
    except Exception:  # noqa: BLE001 (a kernel that cannot roll the ball round the arch's tip)
        shell = shell.faces('>Z').edges().chamfer(EDGE_R * 0.7)
    shell = shell.union(crescent(zf))
    shell = shell.cut(slab(WALL, ZS - 1, zf))
    shell = shell.cut(slab(TONGUE[0] - 0.15, ZS - 1, ZS + TONGUE_H + 0.15).cut(slab(WALL + 1, ZS - 2, ZS + 3)))
    shell = shell.cut(detents(DETENT_GROOVE_R, DETENT_L + 1.0))

    g = display_geometry(display)
    # posts that press the board onto the back shell's tubes (screwed from the back)
    for bx, by in HOLES:
        x, y = xy(bx, by)
        shell = shell.union(cyl(x, y, POST_D / 2, 0, zf + 0.1))
    # two pins through the other display's holes into the board, pointed so that they find them: they line the
    # display's header up with the board's before the two meet
    if display == 'oled':
        pins, pin_d, post_d = TFT_HOLES[:2], 2.2, 4.0
    else:
        pins, pin_d, post_d = OLED_HOLES[2:], 3.0, 5.0
    for bx, by in pins:
        x, y = xy(bx, by)
        r = pin_d / 2 - 0.15
        shell = shell.union(cyl(x, y, post_d / 2, 0, zf + 0.1)).union(cyl(x, y, r, LOCATE_TIP + 1.0, 0.01))
        shell = shell.union(cone(x, y, r * 0.45, r, LOCATE_TIP, LOCATE_TIP + 1.01))
    # the arch's two posts reach down into sockets in the back shell (and take the arch screws)
    for x, y in ARCH_SCREWS:
        post = cyl(x, y, POST_D / 2, ZB + SEAM_GAP, zf + 0.1).faces('<Z').edges().chamfer(0.6)
        shell = shell.union(post)
    # light pipe over the LED
    lx, ly = xy(*LED)
    shell = shell.union(cyl(lx, ly, LIGHT_PIPE_D / 2 + 0.8, 1.9, zf + 0.1))
    shell = shell.union(speaker_mount(zf))
    shell = shell.union(display_mount(display))

    # the display: window (bevelled outside), the glass's pocket, the groove over its header's soldered tails
    cx, cy, aw, ah = g['active']
    ww, wh = aw + 2 * WINDOW_MARGIN, ah + 2 * WINDOW_MARGIN
    shell = shell.cut(rounded_box(cx, cy, ww, wh, 0.8, zf - 1, z_out + 1))
    bevel = (cq.Workplane('XY').workplane(offset=z_out - WINDOW_BEVEL).rect(ww, wh)
             .workplane(offset=WINDOW_BEVEL + 0.01).rect(ww + 2 * WINDOW_BEVEL + 0.02, wh + 2 * WINDOW_BEVEL + 0.02)
             .loft().translate((cx, cy, 0)))
    shell = shell.cut(bevel)
    gx, gy, gw, gh = g['glass']
    shell = shell.cut(box(gx - gw / 2 - GLASS_GAP, gx + gw / 2 + GLASS_GAP,
                          gy - gh / 2 - GLASS_GAP, gy + gh / 2 + GLASS_GAP, zf - 1, zf + GLASS_SEAT))
    span = (g['pins'] - 1) * 2.54
    shell = shell.cut(box(DISPLAY_X - span / 2 - 1.6, DISPLAY_X + span / 2 + 1.6,
                          -g['pins_y'] - 1.6, -g['pins_y'] + 1.6, zf - 1, zf + TAIL_GROOVE))

    # knob (its hole led in from both sides: the knob's cap passes through it as the front goes on), LED, grille,
    # J5's two screws (J3, the speaker's terminal, stays under the face)
    kx, ky = xy(*SWITCH)
    shell = shell.cut(cyl(kx, ky, KNOB_HOLE_D / 2, zf - 1, z_out + 1))
    shell = shell.cut(cone(kx, ky, KNOB_HOLE_D / 2, KNOB_HOLE_D / 2 + 0.41, z_out - 0.4, z_out + 0.01))
    shell = shell.cut(cone(kx, ky, KNOB_HOLE_D / 2 + 0.31, KNOB_HOLE_D / 2, zf - 0.01, zf + 0.3))
    shell = shell.cut(cyl(lx, ly, LIGHT_PIPE_D / 2, 1.0, z_out + 1))
    shell = shell.cut(stars(grille(), zf - 1, z_out + 1))
    for bx, by in J5_SCREWS:
        x, y = xy(bx, by)
        shell = shell.cut(cyl(x, y, TERMINAL_HOLE_D / 2, zf - 1, z_out + 1))
        shell = shell.cut(cone(x, y, TERMINAL_HOLE_D / 2, TERMINAL_HOLE_D / 2 + 0.41, z_out - 0.4, z_out + 0.01))
    # screw holes
    for bx, by in HOLES:
        x, y = xy(bx, by)
        shell = shell.cut(cyl(x, y, SCREW_PILOT / 2, -1, zf + 0.8))
    for x, y in ARCH_SCREWS:
        shell = shell.cut(cyl(x, y, SCREW_PILOT / 2, ZB, zf + 0.8))
    # a fine line engraved round the face, 3 mm in from its edge
    line = slab(3.0, z_out - 0.4, z_out + 1).cut(slab(3.8, z_out - 1, z_out + 2))
    shell = shell.cut(line)
    engraving = face_text(display) if text else None
    if engraving is not None:
        shell = shell.cut(engraving)
    return shell


# ---------------------------------------------------------------- clicks

def detent_spots():
    """Where the beads and grooves sit: (centre X, Y, direction along the wall), on the offset-DETENT_OFFSET line,
    at the middle of long walls (which bow a little as they click) and clear of the openings."""
    d = DETENT_OFFSET
    spots = [((X_L + d, -42.0), (0, 1)), ((X_R - d, -24.0), (0, 1)),
             ((DISPLAY_X - 11.8, Y_BOT + d), (1, 0)), ((DISPLAY_X + 11.8, Y_BOT + d), (1, 0))]
    cl, cr, y = X_L + R_ARCH, X_R - R_ARCH, 14.0
    r = R_ARCH - d
    for c, sign in ((cl, -1), (cr, 1)):          # the arch's lower sides, nearly straight there
        x = c + sign * math.sqrt(r * r - y * y)
        tx, ty = y, -(x - c)                       # tangent of the arc
        n = math.hypot(tx, ty)
        spots.append(((x, y), (tx / n, ty / n)))
    return spots


def detents(radius, length):
    """Round bars along the wall at each detent spot (beads on the tongue, or the grooves that take them)."""
    out = None
    for (x, y), (dx, dy) in detent_spots():
        p = cq.Vector(x - dx * length / 2, y - dy * length / 2, DETENT_Z)
        bar = cq.Workplane('XY').add(cq.Solid.makeCylinder(radius, length, p, cq.Vector(dx, dy, 0)))
        out = bar if out is None else out.union(bar)
    return out


def board_clips():
    """The spring along the board's top edge, two ledges at its bottom edge and four ribs at its sides, on the back
    shell."""
    z_lip = BOARD_PLAY                              # the lips' undersides, just above the board's front surface
    z_root, z_top = ZB - HOOK_ROOT, HOOK_TOP
    t0, t1 = HOOK_T
    y0 = BOARD_PLAY                                 # the wall stands just above the board's top edge (Y = 0)
    xa, xb = HOOK_SPAN
    # the wall tapers from its root to its top; the posts hold its ends, so its middle bends along its length (along
    # the print's layers) as well as from the plate
    parts = (cq.Workplane('YZ').polyline([(y0, z_root), (y0 + t0, z_root), (y0 + t1, z_top), (y0, z_top)]).close()
             .extrude(xb - xa).translate((xa, 0, 0)))
    for px in (xa, xb - HOOK_POST):
        parts = parts.union(box(px, px + HOOK_POST, y0, y0 + t0 + HOOK_POST_T, ZB - 0.01, z_top))
    for hx in HOOKS_X:                              # the lips: a ramp the board's edge pushes, a catch over its front
        parts = parts.union(cq.Workplane('YZ').polyline([(y0 + 0.01, z_lip), (-HOOK_LIP, z_lip),
                                                         (-HOOK_LIP, z_lip + 0.6), (y0 + 0.01, z_top)]).close()
                            .extrude(HOOK_W).translate((hx - HOOK_W / 2, 0, 0)))
    # a slot in the plate under the wall, between the posts, makes its free height longer, unseen outside
    pockets = box(xa + HOOK_POST, xb - HOOK_POST, y0 - 0.6, y0 + t0 + 0.6, z_root, ZB + 0.1)
    yw = Y_BOT + WALL                               # inside of the bottom wall
    for x0, x1 in LEDGES_X:
        ledge = (cq.Workplane('YZ').polyline([(yw - 0.01, z_lip), (-BOARD_H + LEDGE_LIP, z_lip),
                                              (-BOARD_H + LEDGE_LIP, z_lip + 0.7), (yw - 0.01, z_lip + 0.7)]).close()
                 .extrude(x1 - x0).translate((x0, 0, 0)))
        gusset = (cq.Workplane('YZ').polyline([(yw - 0.01, z_lip + 0.01), (yw + 0.5, z_lip + 0.01),
                                               (yw - 0.01, z_lip - 0.5)]).close()
                  .extrude(x1 - x0).translate((x0, 0, 0)))
        parts = parts.union(ledge).union(gusset)
    # ribs from the side walls to the board's edges, below its front surface, chamfered to lead it in
    for y in RIBS_Y:
        for xw, xe, sgn in ((X_L + WALL - 0.01, -RIB_GAP, 1), (X_R - WALL + 0.01, BOARD_W + RIB_GAP, -1)):
            rib = (cq.Workplane('XZ').polyline([(xw, ZB - 0.01), (xe, ZB - 0.01), (xe, -0.8), (xe - sgn * 0.5, -0.3),
                                                (xw, -0.3)]).close()
                   .extrude(RIB_T / 2, both=True).translate((0, y, 0)))
            parts = parts.union(rib)
    return parts, pockets


# ---------------------------------------------------------------- back shell

def back_shell(usbc, clips=True):
    """The back and the rest of the walls; usbc is the side of the board J1 is soldered on ('front' or 'back')."""
    z_in, z_out, z_rim = ZB, ZB - BACK, ZS - SEAM_GAP
    shell = slab(0, z_out, z_rim)
    try:
        shell = shell.faces('<Z').edges().fillet(EDGE_R)
    except Exception:  # noqa: BLE001
        shell = shell.faces('<Z').edges().chamfer(EDGE_R * 0.7)
    shell = shell.cut(slab(WALL, z_in, z_rim + 1))
    # tongue: the inner half of the wall rises into a rebate in the front shell's rim
    tongue = slab(TONGUE[0], z_rim - 0.01, z_rim + TONGUE_H).cut(slab(TONGUE[1], z_rim - 1, z_rim + 2))
    shell = shell.union(tongue)
    if clips:
        shell = shell.union(detents(DETENT_R, DETENT_L))
        board_hooks, pockets = board_clips()
        shell = shell.cut(pockets).union(board_hooks)
    # tubes that press the board against the front shell's posts
    for bx, by in HOLES:
        x, y = xy(bx, by)
        shell = shell.union(cyl(x, y, TUBE_D / 2, z_in - 0.1, -BOARD_T))
    # sockets for the front shell's arch posts, with a lead-in: the shells line up long before the headers meet
    for x, y in ARCH_SCREWS:
        r = POST_D / 2 + GUIDE_CLEAR
        shell = shell.union(cyl(x, y, r + GUIDE_WALL, z_in - 0.1, GUIDE_TOP).intersect(slab(0.5, z_in - 1, ZS)))
        shell = shell.cut(cyl(x, y, r, z_in, GUIDE_TOP + 1)).cut(cone(x, y, r, r + 0.6, GUIDE_TOP - 0.6,
                                                                        GUIDE_TOP + 0.01))
    # screw holes, countersunk from the back
    for x, y in [xy(*h) for h in HOLES] + ARCH_SCREWS:
        shell = shell.cut(cyl(x, y, SCREW_CLEAR / 2, z_out - 1, 0))
        csk = (cq.Workplane('XY').workplane(offset=z_out - 0.01).circle(CSK_D / 2 + 0.01)
               .workplane(offset=(CSK_D - SCREW_CLEAR) / 2 + 0.01).circle(SCREW_CLEAR / 2).loft()
               .translate((x, y, 0)))
        shell = shell.cut(csk)
    # relay wires
    ry = -(J5['y0'] + J5['y1']) / 2
    relay = (cq.Workplane('YZ').center(ry, 3.5).circle(2.5).extrude(WALL + 2.4)
             .union(cq.Workplane('YZ').center(ry, z_rim + 1.6).rect(5.0, 2 * (z_rim + 1.6 - 3.5)).extrude(WALL + 2.4))
             .translate((X_R - WALL - 1.2, 0, 0)))
    shell = shell.cut(relay)
    # USB-C: an opening big enough for common cable plugs, centred on the receptacle
    uy = -(J1['y0'] + J1['y1']) / 2
    if usbc == 'front':
        uz = J1['h'] / 2
        shell = shell.cut(box(X_L - 1, X_L + WALL + 1.2, uy - 6.3, uy + 6.3, uz - 3.5, z_rim + 3))
    else:
        uz = -BOARD_T - J1['h'] / 2
        shell = shell.cut(cq.Workplane('YZ').rect(12.6, 7.0).extrude(WALL + 1.2).edges('|X').fillet(1.0)
                          .translate((X_L - 0.5, uy, uz)))
        shell = shell.cut(box(X_L + WALL - 0.1, J1['x0'] - 0.2, uy - 6.3, uy + 6.3, z_in - 1.0, z_in + 0.1))
    # wall-hanging keyhole in the arch, vents over the amplifier and the module, the name
    kx, ky = DISPLAY_X, 30.0
    key = cyl(kx, ky, 3.8, z_out - 1, z_in + 0.1).union(box(kx - 1.8, kx + 1.8, ky, ky + 7.0, z_out - 1, z_in + 0.1))
    key = key.union(cyl(kx, ky + 7.0, 1.8, z_out - 1, z_in + 0.1))
    shell = shell.cut(key)
    vents = [(DISPLAY_X + (i - 4) * 4.6, -6.5, 1.55) for i in range(9)]
    vents += [(DISPLAY_X + (i - 4) * 4.6, -44.0, 1.55) for i in range(9)]
    shell = shell.cut(stars(vents, z_out - 1, z_in + 0.1))
    plane = cq.Plane(origin=(DISPLAY_X, 17.0, z_out), xDir=(-1, 0, 0), normal=(0, 0, -1))
    name = cq.Workplane(plane).text('ATHAN V3', 4.2, -0.5, combine=False, halign='center', valign='center')
    shell = shell.cut(name)
    return shell


# ---------------------------------------------------------------- knob

def knob(display):
    """Pressed onto the switch's square stem. Its cap is narrower than the face's hole, so the front comes off and
    goes on over it; an eight-pointed star is engraved on the cap, one point up."""
    z0 = STEM_TOP - 3.5                      # bottom, 0.7 mm above the switch's round base
    z_face = face_z(display) + FACE
    zc = z_face + 0.6                        # the cap starts 0.6 mm above the face
    r, rc = KNOB_D / 2, KNOB_HEAD_D / 2
    shaft = cq.Workplane('XY').circle(r).extrude(zc - z0).translate((0, 0, z0))
    flare = cone(0, 0, r, rc, zc, zc + rc - r)
    top = zc + rc - r + 1.0
    k = shaft.union(flare).union(cyl(0, 0, rc, zc + rc - r, top))
    k = k.faces('>Z').edges().fillet(0.6)
    k = k.cut(cq.Workplane('XY').polyline(star_points(0, 0, 3.0)).close().extrude(0.5).translate((0, 0, top - 0.4)))
    sq = STEM_SQ + KNOB_FIT
    k = k.cut(cq.Workplane("XY").rect(sq, sq).extrude(3.52).translate((0, 0, z0 - 0.01)))
    k = k.cut(cq.Workplane('XY').rect(sq + 0.8, sq + 0.8).workplane(offset=0.4).rect(sq, sq).loft()
              .translate((0, 0, z0 - 0.01)))
    kx, ky = xy(*SWITCH)
    return k.translate((kx, ky, 0))


def window_test(display):
    """The part of the face around the display, with its pocket, springs and stops: check the window and the click
    against your module in a few minutes."""
    g = display_geometry(display)
    mx, my, mw, mh = g['module']
    m = 3.0
    top = max(my + mh / 2, -g['pins_y'] + 1.6) + m
    bottom = max(min(my - mh / 2 - m, my - mh / 2 + STOP_FACE - 5.02), Y_BOT + WALL + 0.01)
    zf = face_z(display)
    # the face and what hangs from it, 4.6 mm deep, and the whole of what holds the display
    face = box(mx - mw / 2 - m, mx + mw / 2 + m, bottom, top, zf - 4.6, zf + FACE + 1)
    mount = box(mx - mw / 2 - m, mx + mw / 2 + m, bottom, top, DISPLAY_Z - 2.2, zf + 0.5)
    return front_shell(display, text=False).intersect(face).union(display_mount(display).intersect(mount))


# ---------------------------------------------------------------- export

def parts():
    out = {}
    for disp in ('oled', 'tft'):
        out[f'front_{disp}'] = front_shell(disp)
        out[f'window_test_{disp}'] = window_test(disp)
        out[f'knob_{disp}'] = knob(disp)
    for side in ('back', 'front'):
        out[f'back_usbc_on_{side}'] = back_shell(side)
    return out


def print_pose(name, wp):
    """Parts turned for printing without supports, centred and standing on Z = 0: the front shell face down, the
    back shell back down, the knob on its shaft."""
    if name.startswith(('front', 'window')):
        wp = wp.rotate((0, 0, 0), (1, 0, 0), 180)
    bb = wp.val().BoundingBox(tolerance=0.01)
    return wp.translate((-(bb.xmin + bb.xmax) / 2, -(bb.ymin + bb.ymax) / 2, -bb.zmin))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', default=HERE)
    ap.add_argument('--check', metavar='BOARD_STEP', help="check the parts against the board's STEP model")
    ap.add_argument('--assembly', metavar='BOARD_STEP', help='also write the assembled clock for each display')
    args = ap.parse_args()
    ps = parts()
    os.makedirs(os.path.join(args.out, 'stl'), exist_ok=True)
    os.makedirs(os.path.join(args.out, 'step'), exist_ok=True)
    for name, wp in ps.items():
        cq.exporters.export(print_pose(name, wp), os.path.join(args.out, 'stl', name + '.stl'),
                            tolerance=0.02, angularTolerance=0.15)
        cq.exporters.export(wp.translate(STEP_ORIGIN), os.path.join(args.out, 'step', name + '.step'))
        bb = wp.val().BoundingBox(tolerance=0.01)
        print(f'{name:22s} {bb.xlen:6.1f} x {bb.ylen:6.1f} x {bb.zlen:5.1f} mm')
    if args.check or args.assembly:
        import fitcheck
        if args.check:
            fitcheck.run(ps, args.check)
        if args.assembly:
            for disp in ('tft', 'oled'):
                path = os.path.join(args.out, 'step', f'assembly_{disp}.step')
                fitcheck.assembly(ps, args.assembly, disp).export(path)
                print('wrote', os.path.relpath(path, args.out))


if __name__ == '__main__':
    main()
