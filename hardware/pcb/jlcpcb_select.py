"""Choose which parts JLCPCB assembles: the board's front and back with every part on it. Click a part to take it out
of the JLCPCB order (it turns red: you solder it yourself) or to put it back (green).

generate_fab_files.sh opens this window before it writes the JLCPCB files. On its own, with KiCad's Python:
    <KiCad python> jlcpcb_select.py board.kicad_pcb jlcpcb_selection.json
"Save and continue" writes the choice to jlcpcb_selection.json, which jlcpcb_assembly.py reads; closing the window
keeps the previous choice. --snapshot out.png draws the window's two board views into a picture and exits (a test).

On the board:
- green: JLCPCB places it; red with a cross: taken out, you solder it;
- orange border: JLCPCB cannot supply it now (out of stock or not sold), checked online when the window opens;
- dashed grey: soldered by hand anyway (through-hole, marked DNP, or no LCSC number).

To solder a part JLCPCB lacks before the machine-placed ones, take out its neighbours too: select it (click) and
use "Take out its neighbours".
"""
import json, math, os, subprocess, sys, threading

import wx
APP = wx.App(False)       # before pcbnew, which expects a running wx application
import pcbnew             # noqa: E402

MM = pcbnew.FromMM
PX_PER_MM = 20            # resolution of the drawn board (zoom stays sharp up to about 3x)
COLOURS = {               # JLCPCB's solder mask colours: mask over bare board, mask over copper, silkscreen
    'Green': ('#1d5c2c', '#2f7d3f', '#f2f2f2'),
    'Black': ('#141414', '#2a2a2a', '#f2f2f2'),
    'Blue': ('#163c86', '#2a55aa', '#f2f2f2'),
    'Red': ('#9c1c1c', '#bf3030', '#f2f2f2'),
    'Purple': ('#40235f', '#5a3683', '#f2f2f2'),
    'Yellow': ('#c7ad16', '#dcc443', '#1a1a1a'),
    'White': ('#ececec', '#ffffff', '#1a1a1a'),
}
BACKGROUND = '#3a3a3e'
PAD = '#d9ad3a'           # exposed copper (gold)


# ---------------------------------------------------------------- the board

def rings(ps):
    """SHAPE_POLY_SET -> list of polygons, each [outer, hole, ...] with points in mm."""
    out = []
    for i in range(ps.OutlineCount()):
        poly = [ps.Outline(i)] + [ps.Hole(i, h) for h in range(ps.HoleCount(i))]
        out.append([[(pcbnew.ToMM(c.CPoint(k).x), pcbnew.ToMM(c.CPoint(k).y)) for k in range(c.PointCount())]
                    for c in poly])
    return out


def shape(item, layer):
    ps = pcbnew.SHAPE_POLY_SET()
    if hasattr(item, 'TransformTextToPolySet'):      # texts: the letters themselves, not their box
        item.TransformTextToPolySet(ps, 0, MM(0.01), pcbnew.ERROR_INSIDE)
    else:
        item.TransformShapeToPolygon(ps, layer, 0, MM(0.01), pcbnew.ERROR_INSIDE)
    return rings(ps)


class Part:
    def __init__(self, fp, origin):
        self.ref = fp.GetReference()
        self.value = fp.GetValue()
        self.lcsc = fp.GetFieldText('LCSC') if fp.HasField('LCSC') else ''
        self.side = 'B' if fp.IsFlipped() else 'F'
        a = fp.GetAttributes()
        self.smd = bool(a & pcbnew.FP_SMD)
        self.dnp = fp.IsDNP()
        self.placeable = self.smd and not self.dnp and bool(self.lcsc) and not (a & pcbnew.FP_EXCLUDE_FROM_POS_FILES)
        why = []
        if not self.smd:
            why.append('through-hole' if a & pcbnew.FP_THROUGH_HOLE else 'not a part')
        if self.dnp:
            why.append('marked DNP')
        if self.smd and not self.lcsc:
            why.append('no LCSC number')
        self.why = ', '.join(why)
        # the part's outline: its body (fab layer) and pads; the courtyard only when it has neither
        fab = pcbnew.B_Fab if fp.IsFlipped() else pcbnew.F_Fab
        boxes = [g.GetBoundingBox() for g in fp.GraphicalItems() if g.GetLayer() == fab and g.GetClass() == 'PCB_SHAPE']
        boxes += [q.GetBoundingBox() for q in fp.Pads()]
        if not boxes:
            cy = fp.GetCourtyard(pcbnew.B_CrtYd if fp.IsFlipped() else pcbnew.F_CrtYd)
            boxes = [cy.BBox() if cy.OutlineCount() else fp.GetBoundingBox(False)]
        ox, oy = origin
        self.box = (min(pcbnew.ToMM(bb.GetLeft()) for bb in boxes) - ox, min(pcbnew.ToMM(bb.GetTop()) for bb in boxes) - oy,
                    max(pcbnew.ToMM(bb.GetRight()) for bb in boxes) - ox, max(pcbnew.ToMM(bb.GetBottom()) for bb in boxes) - oy)
        self.stock = None          # (stock, 'basic'/'extended', buyable, reason) once checked


class Board:
    """Everything drawn, in mm from the board's top-left corner, as seen from the front."""

    def __init__(self, path):
        b = pcbnew.LoadBoard(path)
        outline = pcbnew.SHAPE_POLY_SET()
        b.GetBoardPolygonOutlines(outline, False)
        bb = outline.BBox()
        self.ox, self.oy = pcbnew.ToMM(bb.GetLeft()), pcbnew.ToMM(bb.GetTop())
        self.w, self.h = pcbnew.ToMM(bb.GetWidth()), pcbnew.ToMM(bb.GetHeight())
        self.outline = self._move(rings(outline))
        self.layers = {}
        for side, cu, silk in (('F', pcbnew.F_Cu, pcbnew.F_SilkS), ('B', pcbnew.B_Cu, pcbnew.B_SilkS)):
            copper, pads, art = [], [], []
            for z in b.Zones():
                if not z.GetIsRuleArea() and z.IsOnLayer(cu):
                    copper += rings(z.GetFilledPolysList(cu))
            for t in b.GetTracks():
                if t.IsOnLayer(cu):
                    copper += shape(t, cu)
            for fp in b.GetFootprints():
                for p in fp.Pads():
                    if p.IsOnLayer(cu):
                        pads += shape(p, cu)
                for g in list(fp.GraphicalItems()) + [fp.Reference(), fp.Value()]:
                    if g.GetLayer() == silk and (not hasattr(g, 'IsVisible') or g.IsVisible()):
                        art += shape(g, silk)
            for d in b.GetDrawings():
                if d.GetLayer() == silk:
                    art += shape(d, silk)
            self.layers[side] = (self._move(copper), self._move(pads), self._move(art))
        holes = []
        for fp in b.GetFootprints():
            for p in fp.Pads():
                if p.HasHole():
                    ps = pcbnew.SHAPE_POLY_SET()
                    p.TransformHoleToPolygon(ps, 0, MM(0.01), pcbnew.ERROR_INSIDE)
                    holes += rings(ps)
        for v in b.GetTracks():
            if v.GetClass() == 'PCB_VIA':
                c = v.GetPosition(); r = pcbnew.ToMM(v.GetDrillValue()) / 2
                x, y = pcbnew.ToMM(c.x), pcbnew.ToMM(c.y)
                holes.append([[(x + r * math.cos(a / 8 * math.pi), y + r * math.sin(a / 8 * math.pi)) for a in range(16)]])
        self.holes = self._move(holes)
        self.parts = sorted((Part(fp, (self.ox, self.oy)) for fp in b.GetFootprints()), key=natural)

    def _move(self, polys):
        return [[[(x - self.ox, y - self.oy) for x, y in ring] for ring in poly] for poly in polys]


def natural(part):
    ref = part.ref if isinstance(part, Part) else part
    letters = ''.join(ch for ch in ref if ch.isalpha()); digits = ''.join(ch for ch in ref if ch.isdigit())
    return letters, int(digits or 0)


def render(board, side, colour):
    """The board as seen from one side (the back mirrored, as you see it), PX_PER_MM pixels per mm."""
    bare, cu, silk = COLOURS[colour]
    w, h = int(board.w * PX_PER_MM) + 2, int(board.h * PX_PER_MM) + 2
    bmp = wx.Bitmap(w, h)
    dc = wx.MemoryDC(bmp)
    dc.SetBackground(wx.Brush(BACKGROUND)); dc.Clear()
    gc = wx.GraphicsContext.Create(dc)
    k = PX_PER_MM
    mx = (lambda x: (board.w - x) * k) if side == 'B' else (lambda x: x * k)

    def fill(polys, col):
        gc.SetBrush(wx.Brush(col)); gc.SetPen(wx.TRANSPARENT_PEN)
        for poly in polys:
            path = gc.CreatePath()
            for ring in poly:
                if not ring:
                    continue
                path.MoveToPoint(mx(ring[0][0]), ring[0][1] * k)
                for x, y in ring[1:]:
                    path.AddLineToPoint(mx(x), y * k)
                path.CloseSubpath()
            gc.FillPath(path, wx.ODDEVEN_RULE)

    copper, pads, art = board.layers[side]
    fill(board.outline, bare)
    fill(copper, cu)
    fill(pads, PAD)
    fill(art, silk)
    fill(board.holes, BACKGROUND)
    del gc
    dc.SelectObject(wx.NullBitmap)
    return bmp


# ---------------------------------------------------------------- stock, from JLCPCB's parts search

def check_stock(codes, done):
    """For each LCSC number: (stock, library, buyable, reason); None if the lookup failed. Runs in a thread."""
    for code in codes:
        result = None
        try:
            body = json.dumps({'keyword': code, 'currentPage': 1, 'pageSize': 5})
            out = subprocess.run(['curl', '-s', '-m', '20', '-X', 'POST', '-H', 'Content-Type: application/json',
                                  '-d', body, 'https://jlcpcb.com/api/overseas-pcb-order/v1/shoppingCart/'
                                  'smtGood/selectSmtComponentList'], capture_output=True, text=True, timeout=30).stdout
            items = (((json.loads(out).get('data') or {}).get('componentPageInfo') or {}).get('list') or [])
            for c in items:
                if c.get('componentCode') == code:
                    result = (c.get('stockCount') or 0, 'basic' if c.get('componentLibraryType') == 'base' else 'extended',
                              str(c.get('isBuyComponent')) == '1', c.get('noBuyReason') or '')
        except Exception:  # noqa: BLE001  (offline: the window shows "not checked")
            result = None
        wx.CallAfter(done, code, result)


# ---------------------------------------------------------------- the window

class View:
    """Shared by both board views: zoom (1 = fit), pan in pixels, rotation in quarter turns (clockwise)."""
    def __init__(self):
        self.zoom, self.pan, self.quarter = 1.0, [0.0, 0.0], 0


class BoardView(wx.Panel):
    def __init__(self, parent, frame, side):
        super().__init__(parent, style=wx.FULL_REPAINT_ON_RESIZE)
        self.frame, self.side = frame, side
        self.SetBackgroundStyle(wx.BG_STYLE_PAINT)
        self.SetMinSize((420, 360))
        self.Bind(wx.EVT_PAINT, self.on_paint)
        self.Bind(wx.EVT_LEFT_DOWN, self.on_down)
        self.Bind(wx.EVT_LEFT_UP, self.on_up)
        self.Bind(wx.EVT_MOTION, self.on_motion)
        self.Bind(wx.EVT_MOUSEWHEEL, self.on_wheel)
        self.drag = None

    # board mm (seen from this side) <-> window pixels
    def scale(self):
        f, v = self.frame.board, self.frame.view
        w, h = self.GetClientSize()
        bw, bh = (f.w, f.h) if v.quarter % 2 == 0 else (f.h, f.w)
        return min((w - 20) / bw, (h - 20) / bh) * v.zoom

    def to_screen(self, x, y):
        f, v = self.frame.board, self.frame.view
        if self.side == 'B':
            x = f.w - x
        a = v.quarter * math.pi / 2
        u, w_ = x - f.w / 2, y - f.h / 2
        u, w_ = u * math.cos(a) - w_ * math.sin(a), u * math.sin(a) + w_ * math.cos(a)
        cw, ch = self.GetClientSize(); s = self.scale()
        return cw / 2 + v.pan[0] + s * u, ch / 2 + v.pan[1] + s * w_

    def to_board(self, sx, sy):
        f, v = self.frame.board, self.frame.view
        cw, ch = self.GetClientSize(); s = self.scale()
        u, w_ = (sx - cw / 2 - v.pan[0]) / s, (sy - ch / 2 - v.pan[1]) / s
        a = v.quarter * math.pi / 2
        u, w_ = u * math.cos(a) + w_ * math.sin(a), -u * math.sin(a) + w_ * math.cos(a)
        x, y = u + f.w / 2, w_ + f.h / 2
        return (f.w - x if self.side == 'B' else x), y

    def part_at(self, sx, sy):
        x, y = self.to_board(sx, sy)
        hits = [p for p in self.frame.board.parts if p.side == self.side
                and p.box[0] <= x <= p.box[2] and p.box[1] <= y <= p.box[3]]
        hits.sort(key=lambda p: ((p.box[2] - p.box[0]) * (p.box[3] - p.box[1])))   # the smallest one on top
        return hits[0] if hits else None

    def draw(self, gc, size):
        frame, f, v = self.frame, self.frame.board, self.frame.view
        cw, ch = size
        gc.SetBrush(wx.Brush(BACKGROUND)); gc.SetPen(wx.TRANSPARENT_PEN); gc.DrawRectangle(0, 0, cw, ch)
        bmp = frame.bitmaps[self.side]
        s = self.scale()
        gc.PushState()
        gc.Translate(cw / 2 + v.pan[0], ch / 2 + v.pan[1])
        gc.Rotate(v.quarter * math.pi / 2)
        gc.Scale(s, s)
        gc.Translate(-f.w / 2, -f.h / 2)
        gc.DrawBitmap(bmp, 0, 0, bmp.GetWidth() / PX_PER_MM, bmp.GetHeight() / PX_PER_MM)
        gc.PopState()
        font = wx.Font(wx.FontInfo(9).Bold())
        for p in f.parts:
            if p.side != self.side:
                continue
            pts = [self.to_screen(x, y) for x, y in ((p.box[0], p.box[1]), (p.box[2], p.box[1]),
                                                       (p.box[2], p.box[3]), (p.box[0], p.box[3]))]
            xs, ys = [q[0] for q in pts], [q[1] for q in pts]
            x0, y0, x1, y1 = min(xs), min(ys), max(xs), max(ys)
            out = p.ref in frame.excluded
            if p.placeable and not out:
                brush, pen = wx.Brush(wx.Colour(40, 210, 90, 90)), wx.Pen(wx.Colour(60, 230, 110), 2)
            elif p.placeable:
                brush, pen = wx.Brush(wx.Colour(230, 50, 50, 110)), wx.Pen(wx.Colour(255, 80, 80), 2)
            else:
                brush, pen = wx.TRANSPARENT_BRUSH, wx.Pen(wx.Colour(190, 190, 190, 170), 1, wx.PENSTYLE_SHORT_DASH)
            gc.SetBrush(brush); gc.SetPen(pen); gc.DrawRectangle(x0, y0, x1 - x0, y1 - y0)
            if p.placeable and out:
                gc.StrokeLine(x0, y0, x1, y1); gc.StrokeLine(x0, y1, x1, y0)
            if p.placeable and p.stock and (p.stock[0] <= 0 or not p.stock[2]):
                gc.SetBrush(wx.TRANSPARENT_BRUSH); gc.SetPen(wx.Pen(wx.Colour(255, 165, 0), 3))
                gc.DrawRectangle(x0 - 3, y0 - 3, x1 - x0 + 6, y1 - y0 + 6)
            if p.ref == frame.selected:
                gc.SetBrush(wx.TRANSPARENT_BRUSH); gc.SetPen(wx.Pen(wx.Colour(255, 235, 60), 2))
                gc.DrawRectangle(x0 - 5, y0 - 5, x1 - x0 + 10, y1 - y0 + 10)
            gc.SetFont(font, wx.Colour(255, 255, 255) if p.placeable else wx.Colour(220, 220, 220))
            tw, th = gc.GetTextExtent(p.ref)
            if tw < (x1 - x0) + 6 or p.ref == frame.selected:
                gc.DrawText(p.ref, (x0 + x1 - tw) / 2, (y0 + y1 - th) / 2)

    def on_paint(self, _):
        dc = wx.AutoBufferedPaintDC(self)
        gc = wx.GraphicsContext.Create(dc)
        self.draw(gc, self.GetClientSize())

    def on_down(self, e):
        self.drag = (e.GetPosition(), list(self.frame.view.pan), False)
        self.CaptureMouse()

    def on_motion(self, e):
        if self.drag and e.LeftIsDown():
            start, pan, _ = self.drag
            d = e.GetPosition() - start
            if abs(d.x) + abs(d.y) > 4:
                self.drag = (start, pan, True)
                self.frame.view.pan = [pan[0] + d.x, pan[1] + d.y]
                self.frame.refresh_views()
            return
        p = self.part_at(*e.GetPosition())
        self.frame.show_info(p)

    def on_up(self, e):
        if self.HasCapture():
            self.ReleaseMouse()
        if self.drag and not self.drag[2]:
            p = self.part_at(*e.GetPosition())
            if p:
                self.frame.click(p)
        self.drag = None

    def on_wheel(self, e):
        self.frame.zoom(1.15 if e.GetWheelRotation() > 0 else 1 / 1.15)


class Frame(wx.Frame):
    def __init__(self, board, sel_path):
        super().__init__(None, title='JLCPCB assembly: choose the parts JLCPCB places', size=(1460, 860))
        self.board, self.sel_path = board, sel_path
        saved = {}
        if os.path.exists(sel_path):
            try:
                saved = json.load(open(sel_path))
            except ValueError:
                saved = {}
        refs = {p.ref for p in board.parts if p.placeable}
        self.excluded = set(saved.get('excluded', [])) & refs
        self.colour = saved.get('pcb_color', 'Green') if saved.get('pcb_color') in COLOURS else 'Green'
        self.selected = None
        self.view = View()
        self.bitmaps = {s: render(board, s, self.colour) for s in 'FB'}
        self.saved = False

        root = wx.Panel(self)
        main = wx.BoxSizer(wx.HORIZONTAL)
        boards = wx.BoxSizer(wx.HORIZONTAL)
        self.views = {}
        for side, title in (('F', 'Front (as you see it)'), ('B', 'Back (seen from the back)')):
            col = wx.BoxSizer(wx.VERTICAL)
            col.Add(wx.StaticText(root, label=title), 0, wx.LEFT | wx.TOP, 6)
            v = BoardView(root, self, side)
            self.views[side] = v
            col.Add(v, 1, wx.EXPAND | wx.ALL, 4)
            boards.Add(col, 1, wx.EXPAND)
        main.Add(boards, 1, wx.EXPAND)

        side = wx.BoxSizer(wx.VERTICAL)
        row = wx.BoxSizer(wx.HORIZONTAL)
        row.Add(wx.StaticText(root, label='Board colour'), 0, wx.ALIGN_CENTER_VERTICAL | wx.RIGHT, 6)
        self.colour_choice = wx.Choice(root, choices=list(COLOURS))
        self.colour_choice.SetStringSelection(self.colour)
        self.colour_choice.Bind(wx.EVT_CHOICE, self.on_colour)
        row.Add(self.colour_choice, 1)
        side.Add(row, 0, wx.EXPAND | wx.ALL, 6)

        grid = wx.GridSizer(3, 4, 4, 4)
        for label, fn in (('Zoom +', lambda: self.zoom(1.25)), ('Zoom −', lambda: self.zoom(0.8)),
                          ('Turn ⟲', lambda: self.turn(-1)), ('Turn ⟳', lambda: self.turn(1)),
                          ('←', lambda: self.move(-60, 0)), ('→', lambda: self.move(60, 0)),
                          ('↑', lambda: self.move(0, -60)), ('↓', lambda: self.move(0, 60)),
                          ('Fit', self.fit), ('All in', self.all_in), ('All out', self.all_out), ('Undo all', self.undo_all)):
            btn = wx.Button(root, label=label)
            btn.Bind(wx.EVT_BUTTON, lambda _e, f=fn: f())
            grid.Add(btn, 0, wx.EXPAND)
        side.Add(grid, 0, wx.EXPAND | wx.ALL, 6)

        row = wx.BoxSizer(wx.HORIZONTAL)
        self.near = wx.SpinCtrlDouble(root, value='2.0', min=0, max=20, inc=0.5, size=(70, -1))
        btn = wx.Button(root, label='Take out its neighbours')
        btn.Bind(wx.EVT_BUTTON, lambda _e: self.take_out_neighbours())
        row.Add(btn, 0, wx.RIGHT, 6)
        row.Add(wx.StaticText(root, label='within'), 0, wx.ALIGN_CENTER_VERTICAL | wx.RIGHT, 4)
        row.Add(self.near, 0, wx.RIGHT, 4)
        row.Add(wx.StaticText(root, label='mm of the selected part'), 0, wx.ALIGN_CENTER_VERTICAL)
        side.Add(row, 0, wx.ALL, 6)

        self.list = wx.ListCtrl(root, style=wx.LC_REPORT | wx.LC_SINGLE_SEL)
        self.list.EnableCheckBoxes(True)
        for i, (name, width) in enumerate((('Part', 60), ('Value', 120), ('LCSC', 82), ('Library', 66),
                                           ('Stock', 110), ('Side', 46))):
            self.list.InsertColumn(i, name, width=width)
        self.rows = [p for p in board.parts if p.placeable]
        self.updating = True
        for i, p in enumerate(self.rows):
            self.list.InsertItem(i, p.ref)
            self.list.SetItem(i, 1, p.value); self.list.SetItem(i, 2, p.lcsc)
            self.list.SetItem(i, 3, '…'); self.list.SetItem(i, 4, 'checking…')
            self.list.SetItem(i, 5, 'front' if p.side == 'F' else 'back')
            self.list.CheckItem(i, p.ref not in self.excluded)
        self.updating = False
        self.list.Bind(wx.EVT_LIST_ITEM_CHECKED, self.on_check)
        self.list.Bind(wx.EVT_LIST_ITEM_UNCHECKED, self.on_check)
        self.list.Bind(wx.EVT_LIST_ITEM_SELECTED, self.on_row)
        side.Add(wx.StaticText(root, label='Ticked = JLCPCB places it'), 0, wx.LEFT | wx.TOP, 6)
        side.Add(self.list, 1, wx.EXPAND | wx.ALL, 6)

        self.summary = wx.StaticText(root, label='')
        side.Add(self.summary, 0, wx.EXPAND | wx.ALL, 6)
        self.info = wx.StaticText(root, label='Click a part to take it out or put it back. Drag to move, wheel to zoom.')
        side.Add(self.info, 0, wx.EXPAND | wx.ALL, 6)
        row = wx.BoxSizer(wx.HORIZONTAL)
        cancel = wx.Button(root, label='Close without saving')
        cancel.Bind(wx.EVT_BUTTON, lambda _e: self.Close())
        save = wx.Button(root, label='Save and continue')
        save.SetDefault()
        save.Bind(wx.EVT_BUTTON, lambda _e: self.save())
        row.Add(cancel, 0, wx.RIGHT, 8); row.Add(save, 0)
        side.Add(row, 0, wx.ALIGN_RIGHT | wx.ALL, 8)
        main.Add(side, 0, wx.EXPAND)
        root.SetSizer(main)
        self.update_summary()

        codes = sorted({p.lcsc for p in self.rows})
        threading.Thread(target=check_stock, args=(codes, self.on_stock), daemon=True).start()

    # ---- actions
    def refresh_views(self):
        for v in self.views.values():
            v.Refresh()

    def zoom(self, k):
        self.view.zoom = min(max(self.view.zoom * k, 0.3), 8.0); self.refresh_views()

    def turn(self, q):
        self.view.quarter = (self.view.quarter + q) % 4; self.refresh_views()

    def move(self, dx, dy):
        self.view.pan = [self.view.pan[0] + dx, self.view.pan[1] + dy]; self.refresh_views()

    def fit(self):
        self.view.zoom, self.view.pan = 1.0, [0.0, 0.0]; self.refresh_views()

    def set_out(self, refs, out):
        for r in refs:
            (self.excluded.add if out else self.excluded.discard)(r)
        self.updating = True
        for i, p in enumerate(self.rows):
            self.list.CheckItem(i, p.ref not in self.excluded)
        self.updating = False
        self.update_summary(); self.refresh_views()

    def all_in(self):
        self.set_out([p.ref for p in self.rows], False)

    def all_out(self):
        self.set_out([p.ref for p in self.rows], True)

    def undo_all(self):
        saved = {}
        if os.path.exists(self.sel_path):
            try:
                saved = json.load(open(self.sel_path))
            except ValueError:
                pass
        self.excluded = set(saved.get('excluded', [])) & {p.ref for p in self.rows}
        self.set_out([], True)

    def click(self, p):
        self.selected = p.ref
        if p.placeable:
            self.set_out([p.ref], p.ref not in self.excluded)
            i = self.rows.index(p)
            self.list.EnsureVisible(i)
        self.show_info(p); self.refresh_views()

    def take_out_neighbours(self):
        sel = next((p for p in self.board.parts if p.ref == self.selected), None)
        if not sel:
            self.info.SetLabel('Select a part first (click it), then take out its neighbours.'); return
        d = self.near.GetValue()
        near = [p.ref for p in self.rows if p.side == sel.side and p.ref != sel.ref and gap(p.box, sel.box) <= d]
        if sel.placeable:
            near.append(sel.ref)
        self.set_out(near, True)
        self.info.SetLabel(f'Taken out with {sel.ref}: ' + (', '.join(sorted(near, key=natural)) or 'nothing within reach'))

    def on_colour(self, _):
        self.colour = self.colour_choice.GetStringSelection()
        self.bitmaps = {s: render(self.board, s, self.colour) for s in 'FB'}
        self.refresh_views()

    def on_check(self, e):
        if self.updating:
            return
        p = self.rows[e.GetIndex()]
        self.selected = p.ref
        self.set_out([p.ref], not self.list.IsItemChecked(e.GetIndex()))

    def on_row(self, e):
        p = self.rows[e.GetIndex()]
        self.selected = p.ref; self.show_info(p); self.refresh_views()

    def on_stock(self, code, result):
        for i, p in enumerate(self.rows):
            if p.lcsc != code:
                continue
            p.stock = result
            if result is None:
                self.list.SetItem(i, 3, '?'); self.list.SetItem(i, 4, 'not checked')
                continue
            stock, lib, buyable, reason = result
            self.list.SetItem(i, 3, lib)
            self.list.SetItem(i, 4, f'{stock:,}' if buyable and stock > 0 else ('not sold' if not buyable else 'out of stock'))
            if stock <= 0 or not buyable:
                self.list.SetItemTextColour(i, wx.Colour(220, 120, 0))
        self.update_summary(); self.refresh_views()

    def show_info(self, p):
        if not p:
            return
        if p.placeable:
            state = 'you solder it (taken out)' if p.ref in self.excluded else 'JLCPCB places it'
            st = ''
            if p.stock:
                stock, lib, buyable, reason = p.stock
                st = f' · {lib} · ' + (f'{stock:,} in stock' if buyable and stock > 0 else (reason or 'out of stock'))
            self.info.SetLabel(f'{p.ref} · {p.value} · {p.lcsc}{st} · {state}')
        else:
            self.info.SetLabel(f'{p.ref} · {p.value} · soldered by hand ({p.why})')
        self.info.Wrap(380)

    def update_summary(self):
        placed = [p for p in self.rows if p.ref not in self.excluded]
        out = sorted(self.excluded, key=natural)
        lines = len({(p.value, p.lcsc) for p in placed})
        bad = [p.ref for p in placed if p.stock and (p.stock[0] <= 0 or not p.stock[2])]
        text = (f'JLCPCB places {len(placed)} parts ({lines} BOM lines): back '
                f'{sum(p.side == "B" for p in placed)}, front {sum(p.side == "F" for p in placed)} (front parts only in '
                f'the "_both" files).\nTaken out, you solder them: {", ".join(out) or "none"}.')
        if bad:
            text += f'\nCannot be supplied now: {", ".join(bad)}. Take them out (and their neighbours).'
        self.summary.SetLabel(text); self.summary.Wrap(380)
        self.Layout()

    def save(self):
        data = {'excluded': sorted(self.excluded, key=natural), 'pcb_color': self.colour,
                'note': 'Parts left out of the JLCPCB order (written by jlcpcb_select.py).'}
        with open(self.sel_path, 'w') as f:
            json.dump(data, f, indent=2); f.write('\n')
        self.saved = True
        self.Close()


def gap(a, b):
    """Distance between two rectangles (0 when they touch or overlap)."""
    dx = max(a[0] - b[2], b[0] - a[2], 0); dy = max(a[1] - b[3], b[1] - a[3], 0)
    return math.hypot(dx, dy)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    board_file, sel_path = args[0], args[1]
    board = Board(board_file)
    frame = Frame(board, sel_path)
    if '--snapshot' in sys.argv:
        out = sys.argv[sys.argv.index('--snapshot') + 1]
        w, h = 1400, 600
        bmp = wx.Bitmap(w, h)
        dc = wx.MemoryDC(bmp)
        gc = wx.GraphicsContext.Create(dc)
        for i, side in enumerate('FB'):
            v = frame.views[side]
            v.GetClientSize = lambda: wx.Size(w // 2, h)   # draw as if the view were this size
            gc.PushState(); gc.Translate(i * w // 2, 0)
            v.draw(gc, (w // 2, h)); gc.PopState()
        del gc
        dc.SelectObject(wx.NullBitmap)
        bmp.SaveFile(out, wx.BITMAP_TYPE_PNG)
        print('snapshot', out)
        sys.exit(0)
    frame.Show(); frame.Raise()
    APP.MainLoop()
    sys.exit(0 if frame.saved else 1)


if __name__ == '__main__':
    main()
