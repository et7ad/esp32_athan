// The clock's colour screen in the browser: a port of the firmware's drawing (firmware/displays/tft.yaml for the
// clock face and the Wi-Fi setup screen, components/athan/menu_view_tft.cpp for the menu), its menu
// (components/athan/menu.cpp, the rows of athan.yaml's menu_setup) and its keys (athan.yaml's ui_key), running on the
// same data the clocks download: the mosques, their yearly times, the sound list and the radio stations.
// Drawn at the panel's 240 x 240 coordinates in Roboto and Material Symbols, the fonts the firmware uses.

import { NAMES7, dayTimes, loadMosques, getJSON, chosenMosque, remember, parseTZ, localParts } from './site.js';

// ---------------------------------------------------------------- the look (components/athan/tft_theme.h)
const T = {
  FG: '#F2F2EF', SUB: '#A2A8AE', DIM: '#5D646A', LINE: '#2C3136', CARD: '#121518', TRACK: '#1E2226', INK: '#0B0C0D',
  WARN: '#F0A040', BAD: '#F2685C', BLACK: '#000000',
};
export const PALETTES = [
  { name: 'Gold', accent: '#F2B544', athan: '#43C98B' }, { name: 'Teal', accent: '#2CC7B6', athan: '#F2B544' },
  { name: 'Green', accent: '#46C46E', athan: '#F2B544' }, { name: 'Blue', accent: '#5AAEFF', athan: '#F2B544' },
  { name: 'White', accent: '#EDEDEB', athan: '#EDEDEB' },
];
const FONTS = {   // displays/tft.yaml: Roboto Medium unless 400
  t_time: [500, 34], t_big: [500, 28], t_item: [500, 22], t_head: [500, 18], t_row: [500, 16], t_num: [500, 14],
  t_label: [500, 13], t_text: [400, 15], t_small: [400, 13],
};
const SYM = {   // the code points of tft_theme.h, by their Material Symbols names
  RADIO: 'radio', MOSQUE: 'mosque', TWILIGHT: 'wb_twilight', QUEUE_MUSIC: 'queue_music', ALARM: 'alarm', TIMER: 'timer',
  SCHEDULE: 'schedule', TOGGLE_ON: 'toggle_on', VOLUME_UP: 'volume_up', LOCATION: 'location_on',
  CLOCK: 'nest_clock_farsight_analog', BRIGHTNESS: 'brightness_6', DARK_MODE: 'dark_mode', BEDTIME: 'bedtime',
  PALETTE: 'palette', SYSTEM_UPDATE: 'system_update', LOCK: 'lock', INFO: 'info', ARROW_BACK: 'arrow_back',
  CHEVRON_LEFT: 'chevron_left', CHEVRON_RIGHT: 'chevron_right', CHECK: 'check', CHECK_CIRCLE: 'check_circle',
  GRAPHIC_EQ: 'graphic_eq', VOLUME_OFF: 'volume_off', WIFI: 'wifi', WIFI_OFF: 'wifi_off', HISTORY: 'history',
  UPGRADE: 'upgrade', BLUETOOTH: 'bluetooth', HOTSPOT: 'wifi_tethering', POWER: 'power_settings_new',
};
const ELLIPSIS = '…';
const VERSION = '3.0.0';

function blend(a, b, t) {   // tft::blend: a where t = 1
  const p = s => [1, 3, 5].map(i => parseInt(s.slice(i, i + 2), 16));
  const A = p(a), B = p(b);
  return '#' + A.map((v, i) => Math.round(B[i] + (v - B[i]) * t).toString(16).padStart(2, '0')).join('');
}

// ---------------------------------------------------------------- the panel: ESPHome's Display and the tft:: shapes
class Panel {
  constructor(canvas) { this.canvas = canvas; this.c = canvas.getContext('2d'); this.k = 1; }
  fit() {
    const css = this.canvas.getBoundingClientRect().width || 240;
    const k = Math.max(1, Math.min(4, Math.ceil(css * (window.devicePixelRatio || 1) / 240)));
    if (k !== this.k || this.canvas.width !== 240 * k) { this.k = k; this.canvas.width = this.canvas.height = 240 * k; }
    this.c.setTransform(k, 0, 0, k, 0, 0);
  }
  clear() { this.c.fillStyle = T.BLACK; this.c.fillRect(0, 0, 240, 240); }
  font(key) { const [w, s] = FONTS[key]; return `${w} ${s}px Roboto, "Helvetica Neue", Arial, sans-serif`; }
  tw(key, s) { this.c.font = this.font(key); return this.c.measureText(s).width; }
  print(x, y, key, col, al, s) {
    const c = this.c; c.font = this.font(key); c.fillStyle = col; c.textBaseline = 'alphabetic';
    c.textAlign = al === 'c' ? 'center' : al === 'r' ? 'right' : 'left';
    c.fillText(s, x, y);
  }
  rect(x, y, w, h, col) { this.c.fillStyle = col; this.c.fillRect(x, y, w, h); }
  rrect(x, y, w, h, r, col) { const c = this.c; c.fillStyle = col; c.beginPath(); c.roundRect(x, y, w, h, r); c.fill(); }
  rframe(x, y, w, h, r, t, col) {   // a frame t thick inside the box
    const c = this.c; c.strokeStyle = col; c.lineWidth = t;
    c.beginPath(); c.roundRect(x + t / 2, y + t / 2, w - t, h - t, Math.max(0, r - t / 2)); c.stroke();
  }
  circle(cx, cy, r, col) { const c = this.c; c.fillStyle = col; c.beginPath(); c.arc(cx, cy, r, 0, Math.PI * 2); c.fill(); }
  cframe(cx, cy, r, t, col) { const c = this.c; c.strokeStyle = col; c.lineWidth = t; c.beginPath(); c.arc(cx, cy, r, 0, Math.PI * 2); c.stroke(); }
  arc(cx, cy, ro, ri, a0, a1, col) {   // turns, 0 = the top, clockwise
    if (a1 <= a0) return;
    const c = this.c; c.fillStyle = col; c.beginPath();
    if (a1 - a0 >= 1) { c.arc(cx, cy, ro, 0, Math.PI * 2); c.arc(cx, cy, ri, Math.PI * 2, 0, true); }
    else {
      const s = a0 * 2 * Math.PI - Math.PI / 2, e = a1 * 2 * Math.PI - Math.PI / 2;
      c.arc(cx, cy, ro, s, e); c.arc(cx, cy, ri, e, s, true); c.closePath();
    }
    c.fill('evenodd');
  }
  icon(x, y, size, name, col) {   // tft::icon: the icon's square by its top-left corner, on the baseline y + size
    const c = this.c; c.font = `${size}px "Material Symbols Rounded"`; c.fillStyle = col; c.textAlign = 'left';
    c.textBaseline = 'alphabetic'; c.fillText(name, x, y + size);
  }
}

// ---------------------------------------------------------------- text helpers (menu_view.cpp, menu.h)
function fitText(g, key, s, w) {
  if (g.tw(key, s) <= w) return s;
  let t = [...s];
  while (t.length) { t.pop(); const u = t.join('').replace(/\s+$/, ''); if (g.tw(key, u + ELLIPSIS) <= w) return u + ELLIPSIS; }
  return g.tw(key, ELLIPSIS) <= w ? ELLIPSIS : '';
}
function wrapWords(g, key, s, w, maxLines) {
  const out = []; let line = '';
  for (const raw of s.split(' ')) {
    const word = fitText(g, key, raw, w), t = line ? line + ' ' + word : word;
    if (!line || g.tw(key, t) <= w) line = t; else { out.push(line); line = word; }
  }
  if (line) out.push(line);
  if (out.length > maxLines) { out.length = maxLines; out[out.length - 1] = fitText(g, key, out[out.length - 1] + ELLIPSIS, w); }
  return out;
}
function itemBreak(s, width, maxW) {   // menu.h item_break
  const m = /^\d+\.? /.exec(s), afterNumber = m ? m[0].length - 1 : -1;
  let best = -1, bestAlone = false, bestW = 0;
  for (let sp = s.indexOf(' '); sp >= 0; sp = s.indexOf(' ', sp + 1)) {
    const w = Math.max(width(s.slice(0, sp)), width(s.slice(sp + 1)));
    if (w > maxW) continue;
    const alone = sp === afterNumber;
    if (best < 0 || (bestAlone && !alone) || (alone === bestAlone && w < bestW)) { best = sp; bestAlone = alone; bestW = w; }
  }
  return best;
}
function wrapItem(g, key, s, w, maxLines) {
  const words = s.split(' ').filter(Boolean);
  if (words.length >= 2 && /^[\d.]+$/.test(words[0])) words.splice(0, 2, words[0] + ' ' + words[1]);
  const out = []; let line = '';
  for (const word of words) {
    if (g.tw(key, word) > w) return null;
    const t = line ? line + ' ' + word : word;
    if (!line || g.tw(key, t) <= w) line = t; else { out.push(line); line = word; }
  }
  if (line) out.push(line);
  return out.length <= maxLines ? out : null;
}
const ITEM_FONTS = ['t_item', 't_head', 't_small'], ITEM_SIZES = [22, 18, 13];
function itemLines(g, s, w, maxLines = 4) {
  for (let k = 0; k < 3; k++) {
    if (g.tw(ITEM_FONTS[k], s) <= w) return { font: k, lines: [s] };
    if (k === 0) continue;
    const sp = itemBreak(s, l => g.tw(ITEM_FONTS[k], l), w);
    if (sp >= 0) return { font: k, lines: [s.slice(0, sp), s.slice(sp + 1)] };
  }
  const wrapped = wrapItem(g, 't_small', s, w, maxLines);
  if (wrapped) return { font: 2, lines: wrapped };
  return { font: 2, lines: wrapWords(g, 't_small', s, w, maxLines) };
}

// ---------------------------------------------------------------- the menu (components/athan/menu.cpp)
const KIND = { CHOICE: 0, VALUE: 1, ACTION: 2 };
const STYLE = { LIST: 0, CHOICES: 1, LEVEL: 2, TOGGLES: 3, SWATCHES: 4 };
class Menu {
  constructor() { this.rows = []; this.open_ = false; this.row = 0; this.item = -1; this.itemId = 0; this.onClose = null; }
  add(r) { this.rows.push(Object.assign({ kind: KIND.CHOICE, style: STYLE.LIST, wrap: true, exit: false }, r)); }
  get isOpen() { return this.open_; }
  count() { if (!this.open_) return 0; const r = this.rows[this.row]; const n = r.count ? r.count() : 0; return n > 0 ? n : 0; }
  ringRow(off) { const n = this.rows.length; return ((this.row + off) % n + n) % n; }
  open() { if (this.open_ || !this.rows.length) return; this.open_ = true; this.enter(0); }
  close() {
    if (!this.open_) return;
    this.open_ = false;
    const r = this.rows[this.row]; if (r.leave) r.leave();
    if (this.onClose) this.onClose();
  }
  key(k) {
    if (!this.open_) return;
    this.refresh();
    if (k === 'up' || k === 'down') {
      const r = this.ringRow(k === 'up' ? -1 : 1);
      if (r === this.row) return;
      const old = this.rows[this.row]; if (old.leave) old.leave();
      if (this.open_) this.enter(r);
    } else if (k === 'left' || k === 'right') this.move(k === 'left' ? -1 : 1);
    else if (k === 'select') this.select();
  }
  refresh() {
    if (!this.open_) return;
    const row = this.rows[this.row], n = this.count();
    if (n === 0) { this.item = -1; return; }
    if (this.item < 0) { const a = row.active ? row.active() : -1; this.item = a >= 0 && a < n ? a : 0; this.remember(); return; }
    if (row.kind === KIND.VALUE && row.active) { const a = row.active(); if (a >= 0 && a < n) { this.item = a; this.remember(); return; } }
    if (row.idAt && row.find) { const p = row.find(this.itemId); if (p >= 0 && p < n) { this.item = p; return; } }
    if (this.item >= n) this.item = n - 1;
    this.remember();
  }
  enter(r) {
    this.row = r;
    const row = this.rows[r], n = this.count(), a = row.active ? row.active() : -1;
    this.item = n === 0 ? -1 : (a >= 0 && a < n ? a : 0);
    this.remember();
    if (this.item >= 0 && row.show) row.show(this.item, true);
  }
  move(step) {
    const row = this.rows[this.row];
    if (row.exit) { this.close(); return; }
    const n = this.count(); if (n === 0) return;
    let j = (this.item < 0 ? 0 : this.item) + step;
    if (j < 0 || j >= n) { if (!row.wrap) return; j = (j + n) % n; }
    if (j === this.item) return;
    this.item = j; this.remember();
    if (row.kind === KIND.VALUE && row.apply && row.apply(j)) { this.close(); return; }
    if (this.open_ && row.show) row.show(j, false);
  }
  select() {
    const row = this.rows[this.row];
    if (row.exit || row.kind === KIND.VALUE || this.item < 0) { this.close(); return; }
    if (row.kind === KIND.CHOICE && row.active && row.active() === this.item) { this.close(); return; }
    if (!row.apply || row.apply(this.item)) { this.close(); return; }
    this.refresh();
  }
  remember() { const row = this.rows[this.row]; this.itemId = row.idAt && this.item >= 0 ? row.idAt(this.item) : this.item; }
  neighbour(step) {
    const n = this.count();
    if (this.item < 0 || n < 2) return -1;
    const j = this.item + step;
    if (j >= 0 && j < n) return j;
    if (!this.rows[this.row].wrap || n < 3) return -1;
    return (j + n) % n;
  }
  frame() {   // menu_view.cpp frame_of
    this.refresh();
    const row = this.rows[this.row];
    const fr = { title: row.title, kind: row.kind, style: row.style, exit: row.exit, item: this.item, n: this.count(),
      text: '', cap: '', active: -1, inUse: false, playing: false, all: [], shorts: [], ons: [], swatches: [] };
    if (fr.item < 0) { fr.cap = row.caption ? row.caption(-1) : ''; return fr; }
    fr.text = row.label ? row.label(fr.item) : '';
    fr.cap = row.caption ? row.caption(fr.item) : '';
    fr.active = row.active ? row.active() : -1;
    fr.inUse = fr.active === fr.item;
    fr.playing = !!row.playing && row.playing() === fr.item;
    if (fr.n <= 6) {
      for (let i = 0; i < fr.n; i++) {
        if (fr.style === STYLE.CHOICES) fr.all.push(row.label(i));
        else if (fr.style === STYLE.TOGGLES && row.shortLabel) { fr.shorts.push(row.shortLabel(i)); fr.ons.push(row.on(i)); }
        else if (fr.style === STYLE.SWATCHES && row.swatch) fr.swatches.push(row.swatch(i));
      }
    }
    fr.hasLeft = this.neighbour(-1) >= 0;
    fr.hasRight = this.neighbour(1) >= 0;
    return fr;
  }
}

// ---------------------------------------------------------------- drawing the menu (menu_view_tft.cpp)
const MW = 240, LW = 104, ROW_H = 48, PX = LW + 8, PW = MW - PX - 2, PC = PX + Math.floor(PW / 2);
const TOP = 30, BOTTOM = 236, MID = TOP + Math.floor((BOTTOM - TOP) * 42 / 100), ITEM_W = PW - 36;

function roundSpots(n, y) {
  const pts = [], one = Math.floor((PW - 8) / Math.max(1, n));
  if (one >= 34) {
    const gap = Math.min(42, one);
    for (let i = 0; i < n; i++) pts.push([PC - Math.floor(gap * (n - 1) / 2) + i * gap, y]);
    return pts;
  }
  const per = Math.floor((n + 1) / 2), gap = Math.min(40, Math.floor((PW - 8) / per));
  for (let i = 0; i < n; i++) {
    const line = i < per ? 0 : 1, k = line ? i - per : i, cnt = line ? n - per : per;
    pts.push([PC - Math.floor(gap * (cnt - 1) / 2) + k * gap, y - 4 + line * 36]);
  }
  return pts;
}
function caption(g, th, cap) {
  const lines = wrapWords(g, 't_small', cap, PW - 8, 2);
  const y0 = BOTTOM - 8 - (lines.length > 1 ? 17 : 0);
  lines.forEach((l, i) => g.print(PC, y0 + i * 17, 't_small', th.sub, 'c', l));
}
function drawRows(g, menu, th) {
  const n = menu.rows.length;
  for (let k = 0; k < 5; k++) {
    const off = k - 2;
    if (2 * Math.abs(off) >= n + (off > 0 ? 1 : 0)) continue;
    const r = menu.ringRow(off), y = k * ROW_H, sel = off === 0, near = Math.abs(off) === 1, row = menu.rows[r];
    if (sel) g.rrect(4, y + 4, LW - 4, 40, 12, th.acc);
    if (row.symbol) g.icon(12, y + 13, 22, row.symbol, sel ? th.ink : near ? th.acc : th.dim);
    const fits = g.tw('t_row', row.title) <= LW - 44;
    g.print(40, y + 30, 't_row', sel ? th.ink : near ? th.fg : th.dim, 'l', fits || !row.short ? row.title : row.short);
    if (r === 0 && k > 0 && k - 1 !== 2 && !sel) g.rect(10, y, LW - 14, 1, th.line);
  }
  g.rect(LW + 3, 10, 1, 220, th.line);
}
function drawChips(g, fr, th) {
  const pad = 12, gap = 8, ws = [];
  let total = gap * (fr.all.length - 1);
  fr.all.forEach((s, i) => { ws.push(g.tw('t_row', s) + 2 * pad + (i === fr.active ? 18 : 0)); total += ws[i]; });
  if (total > PW - 4) return false;
  let x = PC - Math.floor(total / 2); const y = MID - 16;
  fr.all.forEach((s, i) => {
    const cur = i === fr.item;
    if (cur) g.rrect(x, y, ws[i], 32, 16, th.acc); else g.rframe(x, y, ws[i], 32, 16, 2, th.line);
    g.print(x + pad, y + 22, 't_row', cur ? th.ink : th.fg, 'l', s);
    if (i === fr.active) g.icon(x + pad + g.tw('t_row', s) + 2, y + 8, 16, SYM.CHECK, cur ? th.ink : th.acc);
    x += ws[i] + gap;
  });
  return true;
}
function drawItem(g, fr, th) {
  if (fr.exit) {
    g.print(PC, 104, 't_time', th.fg, 'c', fr.text);
    if (fr.cap) g.print(PC, 130, 't_head', th.acc, 'c', fitText(g, 't_head', fr.cap, PW - 4));
    const y = 218, hw = g.tw('t_small', 'or Select') + 40, x0 = PC - hw / 2;
    g.rframe(PX + 6, y - 14, PW - 12, 30, 15, 2, th.line);
    g.icon(x0, y - 11, 22, SYM.CHEVRON_LEFT, th.sub);
    g.icon(x0 + 16, y - 11, 22, SYM.CHEVRON_RIGHT, th.sub);
    g.print(x0 + 40, y + 5, 't_small', th.sub, 'l', 'or Select');
    return;
  }
  if (fr.item < 0) {
    wrapWords(g, 't_row', fr.cap || 'Empty', PW - 8, 4).forEach((l, i) => g.print(PC, MID + i * 22, 't_row', th.sub, 'c', l));
    return;
  }
  const tabbed = fr.cap.includes('\t');
  if (fr.swatches.length) {
    g.print(PC, MID - 18, 't_head', th.fg, 'c', fr.text);
    roundSpots(fr.swatches.length, MID + 18).forEach(([x, y], i) => {
      g.circle(x, y, 12, fr.swatches[i]);
      if (i === fr.item) g.cframe(x, y, 16, 2, th.fg);
      if (i === fr.active) g.icon(x - 8, y - 8, 16, SYM.CHECK, th.ink);
    });
    return;
  }
  if (fr.shorts.length) {
    const colon = fr.text.indexOf(':');
    const name = (colon < 0 ? fr.text : fr.text.slice(0, colon)) + '  ';
    const on = fr.ons[fr.item], state = on ? 'On' : 'Off';
    const nw = g.tw('t_head', name), x0 = PC - (nw + g.tw('t_head', state)) / 2;
    g.print(x0, MID - 12, 't_head', th.fg, 'l', name);
    g.print(x0 + nw, MID - 12, 't_head', on ? th.acc : th.sub, 'l', state);
    const pts = roundSpots(fr.shorts.length, MID + 22);
    pts.forEach(([px, py], i) => {
      if (fr.ons[i]) g.circle(px, py, 14, th.acc); else g.cframe(px, py, 13, 2, th.dim);
      if (i === fr.item) g.cframe(px, py, 18, 2, th.fg);
      g.print(px, py + 6, 't_row', fr.ons[i] ? th.ink : th.sub, 'c', fr.shorts[i].slice(0, 1));
    });
    if (pts[pts.length - 1][1] < MID + 40) caption(g, th, 'Select: on / off');
    return;
  }
  if (fr.all.length && drawChips(g, fr, th)) { if (fr.cap) caption(g, th, fr.cap); return; }
  if (fr.n === 1) {
    if (tabbed) {
      const ip = /^[\d.]+$/.test(fr.text);
      g.icon(PC - 11, TOP + 6, 22, ip ? SYM.WIFI : SYM.WIFI_OFF, ip ? th.acc : T.BAD);
      const tf = g.tw('t_head', fr.text) <= PW - 4 ? 't_head' : 't_small';
      g.print(PC, 133, tf, th.fg, 'c', fitText(g, tf, fr.text, PW - 4));
      fr.cap.split('\t').slice(0, 2).forEach((s, part) => g.print(PC, part ? 177 : 157, 't_small', th.sub, 'c', fitText(g, 't_small', s, PW - 4)));
      return;
    }
    const label = fitText(g, 't_row', fr.text, PW - 20);
    const bw = Math.min(PW - 8, g.tw('t_row', label) + 32);
    g.rrect(PC - bw / 2, MID - 18, bw, 36, 18, th.acc);
    g.print(PC, MID + 6, 't_row', th.ink, 'c', label);
    if (fr.cap) caption(g, th, fr.cap);
    return;
  }
  const it = itemLines(g, fr.text, ITEM_W, 4), size = ITEM_SIZES[it.font], nl = it.lines.length;
  const lh = size + 4, ty = MID + Math.floor(size * 36 / 100) - Math.floor((nl - 1) * lh / 2);
  it.lines.forEach((l, i) => g.print(PC, ty + i * lh, ITEM_FONTS[it.font], th.fg, 'c', l));
  if (fr.hasLeft) g.icon(PX, MID - 11, 22, SYM.CHEVRON_LEFT, th.sub);
  if (fr.hasRight) g.icon(PX + PW - 22, MID - 11, 22, SYM.CHEVRON_RIGHT, th.sub);
  const marks = [];
  if (fr.inUse && fr.kind !== KIND.VALUE) marks.push(SYM.CHECK_CIRCLE);
  if (fr.playing) marks.push(SYM.GRAPHIC_EQ);
  const my = ty + (nl - 1) * lh + 8;
  marks.forEach((m, i) => g.icon(PC - marks.length * 11 + i * 22, my, 22, m, th.acc));
  if (fr.style === STYLE.LEVEL && fr.n > 1) {
    const bx = PX + 14, bw = PW - 28, y = my + 6;
    g.rrect(bx, y, bw, 10, 5, th.line);
    if (fr.item > 0) g.rrect(bx, y, Math.max(10, Math.floor(bw * fr.item / (fr.n - 1))), 10, 5, th.acc);
    if (fr.cap) caption(g, th, fr.cap);
  } else if (fr.cap) {
    caption(g, th, fr.cap);
  } else if (fr.n > 1 && fr.n <= 12) {
    const y = BOTTOM - 12, gap = Math.min(13, Math.floor((PW - 28) / (fr.n - 1))), x0 = PC - Math.floor((fr.n - 1) * gap / 2);
    for (let i = 0; i < fr.n; i++) { if (i === fr.item) g.circle(x0 + i * gap, y, 3.5, th.acc); else g.circle(x0 + i * gap, y, 2, th.dim); }
  } else if (fr.n > 12) {
    const y = BOTTOM - 12, track = PW - 20, x0 = PC - track / 2;
    g.rect(x0, y - 1, track, 3, th.line);
    g.circle(x0 + Math.floor(fr.item * track / (fr.n - 1)), y + 0.5, 5, th.acc);
  }
}
function drawMenu(g, menu, acc) {
  const fr = menu.frame();
  const th = { acc, fg: T.FG, sub: T.SUB, dim: T.DIM, line: T.LINE, ink: T.INK };
  drawRows(g, menu, th);
  g.print(PC, 22, 't_label', acc, 'c', fr.title.toUpperCase());
  drawItem(g, fr, th);
}

// ---------------------------------------------------------------- windows (components/athan/day_window.h)
const WP = 100, WINDOW_CHOICES = 26;
function windowMinute(v, times) { if (v >= WP) return times && times[v - WP] !== undefined ? times[v - WP] : 0; return (v < 0 || v > 23 ? 0 : v) * 60; }
function inWindow(from, until, t, times) { const a = windowMinute(from, times), b = windowMinute(until, times); if (a === b) return true; return a < b ? t >= a && t < b : t >= a || t < b; }
function windowLength(from, until, times) { const a = windowMinute(from, times), b = windowMinute(until, times); return a === b ? 1440 : (b - a + 1440) % 1440; }
function windowValue(night, until, i) {
  if (i === 0 || i === 1) { const dawn = night === until; return WP + (dawn ? (i === 0 ? 0 : 3) : (i === 0 ? 5 : 6)); }
  return i - 2 < 0 || i - 2 > 23 ? 0 : i - 2;
}
function windowChoice(night, until, v) {
  if (v === windowValue(night, until, 0)) return 0;
  if (v === windowValue(night, until, 1)) return 1;
  return v >= 0 && v <= 23 ? v + 2 : -1;
}
function nightLevel(mode, bright, dim) { return mode === 2 ? 0 : mode === 1 ? Math.min(bright, dim) : bright; }

// ---------------------------------------------------------------- sound (optional: the visitor switches it on)
const dbGain = pct => (pct <= 0 ? 0 : Math.pow(10, ((pct / 100) - 1) * 30 / 20));   // the clock maps % onto 30 dB
class Speaker {
  constructor() { this.on = false; this.a = null; this.kind = null; }
  play(url, pct, kind) {
    this.stop();
    if (!this.on || !url) return;
    this.a = new Audio(url); this.a.volume = dbGain(pct); this.kind = kind;
    this.a.play().catch(() => {});
  }
  volume(pct) { if (this.a) this.a.volume = dbGain(pct); }
  stop(kind) { if (this.a && (!kind || this.kind === kind)) { this.a.pause(); this.a.src = ''; this.a = null; this.kind = null; } }
}

// ---------------------------------------------------------------- the clock
const IDX5 = [0, 3, 4, 5, 6], FIVE = ['Fajr', 'Dhuhr', 'Asr', 'Maghrib', 'Isha'];
const DAYS = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
const SLOTS = ['athan', 'fajr', 'tawashih', 'tick'];
const OFF = -1, RANDOM = -2;

let fontsReady = null;
function loadFonts() {
  if (!fontsReady) {
    fontsReady = Promise.all(['400 15px Roboto', '500 15px Roboto'].map(f => document.fonts.load(f))
      .concat([document.fonts.load('24px "Material Symbols Rounded"', 'wifi_off')])).catch(() => {});
  }
  return fontsReady;
}

export class Clock {
  /** canvas: a <canvas>; opts: { mode: 'clock' | 'setup', onChange(clock) } */
  constructor(canvas, opts = {}) {
    this.g = new Panel(canvas);
    this.canvas = canvas;
    this.opts = opts;
    this.speaker = new Speaker();
    this.offsetMs = 0;          // the simulated time is now + offset
    this.s = {
      clock12: false, palette: 1, athanOn: [true, true, true, true, true, true, true], volume: 50, fajrVolume: -1,
      brightness: 100, night: 0, nightFrom: 22, nightUntil: 6, tickOn: true, tickFrom: 0, tickUntil: 0, prefajr: false,
      radioSlot: 0, radio: -1, locked: false, relay: false, wifi: true, update: null, standinForced: false,
      sound: [0, 0, RANDOM, 0], download: null, toast: 0, woken: -1e9, forceDark: false, athan: null,
      setup: opts.mode === 'setup' ? { ble: 'on', ap: true } : null, checking: 0, installing: 0,
    };
    this.day = null;            // { key, ymd, times, standin }
    this.menu = new Menu();
    this.lastMinute = -1;
    this.flashStart = 0;
    canvas.tabIndex = 0;
    canvas.setAttribute('role', 'img');
    canvas.addEventListener('keydown', ev => {
      const k = { ArrowUp: 'up', ArrowDown: 'down', ArrowLeft: 'left', ArrowRight: 'right', Enter: 'select', ' ': 'select' }[ev.key];
      if (k) { ev.preventDefault(); this.key(k); }
    });
  }

  async init(mosqueKey) {
    const [mosques, catalog, stations] = await Promise.all([loadMosques(), getJSON('audio/catalog.json'), getJSON('radio/stations.json')]);
    this.mosques = mosques;
    this.catalog = SLOTS.map(s => (catalog && catalog[s]) || []);
    this.stations = (stations && stations.stations) || [];
    this.mosque = (mosqueKey && mosques.find(m => m.key === mosqueKey)) || chosenMosque(mosques);
    this.buildMenu();
    await loadFonts();
    await this.loadDay(true);
    this.tick();
    if (typeof ResizeObserver !== 'undefined') new ResizeObserver(() => this.draw()).observe(this.canvas);
    return this;
  }

  // ---- time and today's times, at the mosque
  nowMs() { return Date.now() + this.offsetMs; }
  local() { return localParts(this.tz, this.nowMs()); }
  get tz() { return parseTZ((this.mosque && this.mosque.tz) || 'UTC0'); }
  async loadDay(force) {
    if (!this.mosque) return;
    const p = this.local(), ymd = `${this.mosque.key}-${p.y}-${p.doy}`;
    if (!force && this.day && this.day.ymd === ymd) return;
    const d = await dayTimes(this.mosque, this.nowMs());
    this.day = { ymd, times: d.times, standin: d.standin };
  }
  setMosque(key) {
    const m = this.mosques.find(x => x.key === key);
    if (!m) return;
    this.mosque = m;
    remember('athan-mosque', key);
    this.loadDay(true).then(() => this.draw());
  }

  /** What the clock knows now: the snapshot the drawing reads (the firmware's globals). */
  state() {
    const s = this.s, p = this.local(), times = this.day && this.day.times;
    const setup = !!s.setup;
    const timeOk = !setup;
    const cur = p.h * 60 + p.mi, nowS = cur * 60 + p.s;
    let npi = 0;
    if (times) for (let i = 0; i < 7; i++) if (times[i] > cur) { npi = i; break; }
    return { p, cur, nowS, times, timeOk, timesOk: timeOk && !!times, npi, setup };
  }

  // ---- the seconds: athans, the pop-ups, the dark screen
  tick() {
    clearTimeout(this.timer);
    const st = this.state(), s = this.s, now = performance.now();
    if (st.timeOk) this.loadDay(false).then(() => {});
    if (s.athan && Date.now() > s.athan.until) this.endAthan();
    if (st.timesOk && st.cur !== this.lastMinute) {
      if (this.lastMinute >= 0) {
        for (const i of [0, 3, 4, 5, 6]) {
          if (st.times[i] === st.cur && s.athanOn[i] && !s.athan) this.startAthan(i);
        }
        if (st.p.mi === 0 && s.tickOn && !s.athan && !this.previewing && inWindow(s.tickFrom, s.tickUntil, st.cur, st.times)) {
          const e = this.catalog[3][s.sound[3]];
          if (e && this.speaker.on && !this.speaker.a) this.speaker.play('audio/' + e.url, s.volume, 'tick');
        }
      }
      this.lastMinute = st.cur;
    }
    if (s.toast && now > s.toastUntil) s.toast = 0;
    if (this.menu.isOpen && Date.now() - this.lastKey > 60000) this.menu.close();
    this.draw();
    // the next second; while the ring flashes also its half second (500 ms lit, 500 dark, as tft_flash_ms)
    const ms = this.nowMs() % 1000;
    this.timer = setTimeout(() => this.tick(), (this.flashing && ms < 500 ? 500 - ms : 1000 - ms) + 5);
  }

  startAthan(i) {
    const s = this.s, fajr = i === 0;
    const list = this.catalog[fajr ? 1 : 0], e = list[s.sound[fajr ? 1 : 0]];
    const len = (e && this.durations && this.durations[e.url]) || 180;
    s.athan = { idx: i, until: Date.now() + len * 1000, radioResume: s.radio };
    if (s.radio >= 0) { s.radio = -1; this.speaker.stop('radio'); }
    s.woken = performance.now();
    if (e) this.speaker.play('audio/' + e.url, fajr ? this.fajrVol() : s.volume, 'athan');
    this.changed();
  }
  endAthan(stopped) {
    const s = this.s, resume = s.athan && s.athan.radioResume;
    s.athan = null;
    this.speaker.stop('athan');
    if (!stopped && resume >= 0) this.radioStart(resume);   // the same station comes back; stopping the athan leaves it off
    s.woken = performance.now();
    this.changed();
  }
  fajrVol() { return this.s.fajrVolume < 0 ? this.s.volume : this.s.fajrVolume; }
  radioStart(slot) {
    const s = this.s;
    s.radioSlot = slot; s.radio = slot;
    const st = this.stations[slot];
    this.speaker.play(st && st.url, s.volume, 'radio');
  }
  radioOff() { this.s.radio = -1; this.speaker.stop('radio'); }
  stopSound() {   // stop_sound: the athan, a preview, the tick; the radio plays on
    const s = this.s;
    if (s.athan) this.endAthan(true);
    if (this.previewing) { this.previewing = null; this.speaker.stop('preview'); }
    if (this.speaker.kind === 'tick') this.speaker.stop('tick');
  }
  get soundOn() { return !!this.s.athan || !!this.previewing || this.speaker.kind === 'tick'; }

  /** The screen's level (screen_refresh): 0 = dark. */
  level() {
    const s = this.s, st = this.state(), dim = s.brightness > 0 ? Math.min(s.brightness, 10) : 10;
    const night = s.night !== 0 && st.timeOk && inWindow(s.nightFrom, s.nightUntil, st.cur, st.times);
    let pct = night ? nightLevel(s.night, s.brightness, dim) : s.brightness, darkIn = -1;
    if (pct === 0 && !s.forceDark) {
      const since = performance.now() - s.woken;
      if (s.athan) pct = dim;
      else if (since < 120000) { pct = dim; darkIn = Math.ceil((120000 - since) / 1000); }
    }
    return { pct, darkIn };
  }

  // ---- the keys (ui_key)
  key(k) {
    const s = this.s;
    this.lastKey = Date.now();
    if (this.opts.onKey) this.opts.onKey(k);
    if (s.setup) { this.setupKey(k); return; }
    const dark = this.level().pct === 0;
    s.woken = performance.now();
    if (dark) { s.forceDark = false; this.changed(); return; }
    if (s.locked) { this.stopSound(); this.radioOff(); this.changed(); return; }
    if (this.menu.isOpen) { this.menu.key(k); this.changed(); return; }
    if (k === 'left' || k === 'right') {
      const fajr = (s.athan && s.athan.idx === 0) || (this.previewing && this.previewing.slot <= 2 && this.previewing.slot >= 1);
      const v = fajr ? this.fajrVol() : s.volume, d = k === 'right' ? 1 : -1;
      let nv = Math.min(100, v + 10 * d);
      if (d < 0) nv = Math.max(nv, Math.min(v, 10));
      if (fajr) s.fajrVolume = nv; else s.volume = nv;
      this.speaker.volume(nv);
      this.toast(fajr ? 2 : 1);
      return;
    }
    if (this.soundOn) { this.stopSound(); this.changed(); return; }
    if (k === 'up') { if (s.radio >= 0) this.radioOff(); else this.radioStart(s.radioSlot); }
    else if (k === 'down') { s.relay = !s.relay; this.toast(3); return; }
    else { this.menuRadioResume = -1; this.menu.open(); }
    this.changed();
  }
  /** Opens the menu on the row with this title (the menu table on the use page). */
  openRow(title) {
    const s = this.s;
    if (s.setup) return;
    s.locked = false; s.forceDark = false; s.woken = performance.now();
    if (this.soundOn) this.stopSound();
    if (!this.menu.isOpen) this.menu.open();
    const r = this.menu.rows.findIndex(x => x.title === title);
    if (r >= 0 && r !== this.menu.row) { const old = this.menu.rows[this.menu.row]; if (old.leave) old.leave(); this.menu.enter(r); }
    this.lastKey = Date.now();
    this.changed();
  }
  toast(kind) { this.s.toast = kind; this.s.toastUntil = performance.now() + 2000; this.changed(); }
  changed() { this.draw(); if (this.opts.onChange) this.opts.onChange(this); }

  // ---- the Wi-Fi setup screen, step by step (setup page)
  setupKey(k) {
    const su = this.s.setup;
    if (k === 'select' && su.ble === 'ask') { su.ble = 'allowed'; this.changed(); }
  }
  setupStep(step) {
    const su = this.s.setup;
    if (!su) return;
    if (step === 'phone') su.ble = 'ask';
    else if (step === 'send') { su.ble = 'joining'; this.changed(); setTimeout(() => this.setupDone(), 2400); return; }
    this.changed();
  }
  setupDone() { this.s.setup = null; this.loadDay(true).then(() => this.changed()); }
  restartSetup() { this.s.setup = { ble: 'on', ap: true }; this.menu.close(); this.changed(); }

  // ---- scenarios (use page): moments of the day and the marks
  async scenario(id) {
    const s = this.s, st = this.state();
    if (id === 'now') { this.offsetMs = 0; }
    if ((id === 'lastminute' || id === 'athan') && st.times) {
      // the next athan (not Sunrise or Doha), then a little before it
      let i = st.npi; while (i === 1 || i === 2) i++;
      const at = st.times[i] * 60 - (id === 'athan' ? 3 : 70);
      let dt = at - st.nowS; if (dt < 0) dt += 86400;
      this.offsetMs += dt * 1000 - (this.nowMs() % 1000);
    }
    if (s.athan) this.endAthan(true);
    this.lastMinute = this.state().cur;   // the minute the jump lands in: its prayer comes with the next minute
    await this.loadDay(true);
    this.tick();
    this.changed();
  }

  // ---- the menu's rows (athan.yaml menu_setup, tft.yaml menu_display_rows)
  buildMenu() {
    const s = this.s, m = this.menu, self = this;
    const hourText = h => (s.clock12 ? `${h % 12 || 12} ${h < 12 ? 'AM' : 'PM'}` : `${String(h).padStart(2, '0')}:00`);
    const endText = v => (v >= WP ? NAMES7[v - WP] : hourText(v));
    const winCaption = (from, until, isUntil) => {
      const len = windowLength(from, until, this.day && this.day.times);
      if (len === 1440) return 'All day';
      return (isUntil ? 'from ' + endText(from) : 'until ' + endText(until)) + ', ' + Math.floor((len + 30) / 60) + ' h';
    };
    m.add({
      title: 'Radio', symbol: SYM.RADIO, kind: KIND.ACTION, count: () => 10,
      label: i => { const nm = this.stations[i] && this.stations[i].name; return nm ? `${i + 1} ${nm}` : `Radio ${i + 1}`; },
      active: () => s.radioSlot, playing: () => s.radio,
      caption: i => (i < 0 || (this.stations[i] && this.stations[i].url) ? '' : 'No link here'),
      apply: i => { if (!(this.stations[i] && this.stations[i].url)) return false; this.menuRadioResume = -1; if (s.radio === i) this.radioOff(); else this.radioStart(i); return true; },
    });
    const soundRow = (title, slot, extra) => {
      const off = slot === 3, rnd = slot === 2;
      const head = off ? [OFF] : rnd ? [RANDOM] : [];
      const itemAt = i => (i < head.length ? head[i] : i - head.length);
      const posOf = it => (it < 0 ? head.indexOf(it) : it + head.length);
      const name = it => `${it + 1}. ${this.catalog[slot][it].name}`;
      return Object.assign({
        title, count: () => head.length + this.catalog[slot].length, idAt: itemAt, find: posOf,
        label: i => { const it = itemAt(i); return it === OFF ? 'Off' : it === RANDOM ? 'Random' : name(it); },
        active: () => (off && !s.tickOn ? posOf(OFF) : posOf(s.sound[slot])),
        caption: i => {
          if (rnd && !(s.download && s.download.slot === slot) && i >= 0 && itemAt(i) === RANDOM) return 'Random one every day';
          if (i < 0) return 'List is empty';
          if (s.download && s.download.slot === slot) return s.download.status;
          return '';
        },
        show: (i, entered) => {
          const it = itemAt(i);
          clearTimeout(this.previewTimer);
          if (s.athan) return;
          if (it < 0) { this.stopPreview(); return; }
          if (s.radio >= 0) { if (entered) return; this.menuRadioResume = s.radio; }
          this.previewTimer = setTimeout(() => this.preview(slot, it), entered ? 800 : 500);
        },
        apply: i => {
          const it = itemAt(i);
          this.stopPreview();
          if (it === OFF) { s.tickOn = false; return false; }
          if (off) s.tickOn = true;
          if (it === RANDOM) { s.sound[slot] = RANDOM; s.download = null; return false; }
          this.download(slot, it, name(it));
          return false;
        },
        leave: () => this.stopPreview(),
      }, extra);
    };
    m.add(soundRow('Athan', 0, { symbol: SYM.MOSQUE }));
    m.add(soundRow('Fajr Athan', 1, { short: 'Fajr', symbol: SYM.TWILIGHT }));
    m.add(soundRow('Tawashih', 2, { short: 'Tawsh', symbol: SYM.QUEUE_MUSIC }));
    m.add({
      title: 'Pre-Fajr', symbol: SYM.ALARM, style: STYLE.CHOICES, count: () => 2, label: i => (i ? 'On' : 'Off'),
      active: () => (s.prefajr ? 1 : 0), caption: () => 'Tawashih 25 min before Fajr', apply: i => { s.prefajr = i === 1; return false; },
    });
    m.add(soundRow('Hourly Tick', 3, { short: 'Tick', symbol: SYM.TIMER }));
    for (let w = 0; w < 2; w++) {
      m.add({
        title: w ? 'Tick Until' : 'Tick From', short: w ? 'Until' : 'From', symbol: SYM.SCHEDULE, count: () => WINDOW_CHOICES,
        label: i => endText(windowValue(false, w === 1, i)),
        active: () => windowChoice(false, w === 1, w ? s.tickUntil : s.tickFrom),
        caption: i => { const v = windowValue(false, w === 1, Math.max(0, i)); return w ? winCaption(s.tickFrom, v, true) : winCaption(v, s.tickUntil, false); },
        apply: i => { if (w) s.tickUntil = windowValue(false, true, i); else s.tickFrom = windowValue(false, false, i); return false; },
      });
    }
    const pOf = [0, 3, 4, 5, 6], shorts = ['Fajr', 'Dhuhr', 'Asr', 'Magh', 'Isha'];
    m.add({
      title: 'Athan On/Off', short: 'On/Off', symbol: SYM.TOGGLE_ON, kind: KIND.ACTION, style: STYLE.TOGGLES, count: () => 5,
      shortLabel: i => shorts[i], on: i => s.athanOn[pOf[i]],
      label: i => FIVE[i] + (s.athanOn[pOf[i]] ? ': On' : ': Off'),
      apply: i => { s.athanOn[pOf[i]] = !s.athanOn[pOf[i]]; return false; },
    });
    m.add({
      title: 'Volume', short: 'Vol', symbol: SYM.VOLUME_UP, kind: KIND.VALUE, style: STYLE.LEVEL, wrap: false, count: () => 11,
      label: i => `${i * 10}%`, active: () => Math.max(0, Math.min(10, Math.floor((s.volume + 5) / 10))),
      apply: i => { s.volume = i * 10; this.speaker.volume(s.volume); return false; },
    });
    m.add({
      title: 'Fajr Volume', short: 'Fajr', symbol: SYM.VOLUME_UP, kind: KIND.VALUE, style: STYLE.LEVEL, wrap: false, count: () => 11,
      label: i => `${i * 10}%`, active: () => Math.max(0, Math.min(10, Math.floor((this.fajrVol() + 5) / 10))),
      apply: i => { s.fajrVolume = i * 10; return false; },
    });
    m.add({
      title: 'Location', short: 'Masjid', symbol: SYM.LOCATION, style: STYLE.CHOICES, count: () => this.mosques.length,
      idAt: i => i, find: i => i, label: i => this.mosques[i].label,
      active: () => this.mosques.indexOf(this.mosque),
      caption: i => { const q = this.mosques[i]; return q && typeof q.lat === 'number' ? `${q.lat.toFixed(4)}, ${q.lon.toFixed(4)}` : ''; },
      apply: i => { this.setMosque(this.mosques[i].key); return false; },
    });
    m.add({
      title: 'Clock', symbol: SYM.CLOCK, style: STYLE.CHOICES, count: () => 2, label: i => (i ? '12-hour' : '24-hour'),
      active: () => (s.clock12 ? 1 : 0), apply: i => { s.clock12 = i === 1; return false; },
    });
    m.add({
      title: 'Brightness', short: 'Bright', symbol: SYM.BRIGHTNESS, kind: KIND.VALUE, style: STYLE.LEVEL, wrap: false, count: () => 11,
      label: i => `${i * 10}%`, active: () => Math.max(0, Math.min(10, Math.floor(s.brightness / 10))),
      caption: i => (i === 0 ? 'Dark till a key' : ''),
      apply: i => { s.brightness = i * 10; s.forceDark = i === 0; return false; },
    });
    const modes = ['Normal', 'Dim', 'Turned off'];
    m.add({
      title: 'Screen at Night', short: 'Night', symbol: SYM.DARK_MODE, style: STYLE.CHOICES, count: () => 3, label: i => modes[i],
      active: () => s.night,
      caption: i => {
        if (i <= 0) return 'Same as by day';
        return windowLength(s.nightFrom, s.nightUntil, this.day && this.day.times) === 1440 ? 'All day' : endText(s.nightFrom) + ' to ' + endText(s.nightUntil);
      },
      apply: i => { s.night = i; return false; },
    });
    for (let w = 0; w < 2; w++) {
      m.add({
        title: w ? 'Night Until' : 'Night From', short: w ? 'Until' : 'From', symbol: SYM.BEDTIME, count: () => WINDOW_CHOICES,
        label: i => endText(windowValue(true, w === 1, i)),
        active: () => windowChoice(true, w === 1, w ? s.nightUntil : s.nightFrom),
        caption: i => { const v = windowValue(true, w === 1, Math.max(0, i)); return w ? winCaption(s.nightFrom, v, true) : winCaption(v, s.nightUntil, false); },
        apply: i => { if (w) s.nightUntil = windowValue(true, true, i); else s.nightFrom = windowValue(true, false, i); return false; },
      });
    }
    m.add({
      title: 'Screen Colour', short: 'Colour', symbol: SYM.PALETTE, style: STYLE.SWATCHES, count: () => PALETTES.length,
      label: i => PALETTES[i].name, swatch: i => PALETTES[i].accent, active: () => s.palette, apply: i => { s.palette = i; return false; },
    });
    m.add({
      title: 'Update', symbol: SYM.SYSTEM_UPDATE, kind: KIND.ACTION, count: () => 1,
      label: () => (s.installing ? 'Installing...' : s.checking ? 'Checking...' : s.update ? 'Install ' + s.update : 'Check now'),
      caption: () => (s.update ? 'Installed: V' + VERSION : s.checked ? `V${VERSION} is the latest` : 'Installed: V' + VERSION),
      apply: () => {
        if (s.update) {
          s.installing = 1; this.changed();
          setTimeout(() => { s.installing = 0; s.update = null; this.menu.close(); this.changed(); }, 3000);
        } else if (!s.checking) {
          s.checking = 1;
          setTimeout(() => { s.checking = 0; s.checked = true; this.changed(); }, 1500);
        }
        return false;
      },
    });
    m.add({
      title: 'Lock Buttons', short: 'Lock', symbol: SYM.LOCK, kind: KIND.ACTION, count: () => 1, label: () => 'Lock now',
      caption: () => 'Unlock: hold arrow at boot', apply: () => { s.locked = true; return true; },
    });
    m.add({
      title: 'Info', symbol: SYM.INFO, kind: KIND.ACTION, count: () => 1, label: () => (s.wifi ? '192.168.1.42' : 'No Wi-Fi'),
      caption: () => 'athan.local\tV' + VERSION, apply: () => true,
    });
    m.add({
      title: 'Exit', symbol: SYM.ARROW_BACK, kind: KIND.ACTION, exit: true, count: () => 1,
      label: () => { const p = this.local(); return s.clock12 ? `${p.h % 12 || 12}:${String(p.mi).padStart(2, '0')}` : `${String(p.h).padStart(2, '0')}:${String(p.mi).padStart(2, '0')}`; },
      caption: () => {
        const st = this.state(); if (!st.timesOk) return '';
        const t = st.times[st.npi], h = Math.floor(t / 60), mm = String(t % 60).padStart(2, '0');
        return s.clock12 ? `${NAMES7[st.npi]} ${h % 12 || 12}:${mm}` : `${NAMES7[st.npi]} ${String(h).padStart(2, '0')}:${mm}`;
      },
    });
    m.onClose = () => {
      this.s.download = this.s.download && this.s.download.done ? null : this.s.download;
      const r = this.menuRadioResume; this.menuRadioResume = -1;
      if (r >= 0 && s.radio < 0 && !s.athan) this.radioStart(r);
    };
  }
  preview(slot, it) {
    const e = this.catalog[slot][it];
    if (!e) return;
    this.previewing = { slot, it };
    if (this.s.radio >= 0) { this.s.radio = -1; this.speaker.stop('radio'); }
    const vol = slot === 1 || slot === 2 ? this.fajrVol() : this.s.volume;
    this.speaker.play('audio/' + e.url, vol, 'preview');
    this.changed();
  }
  stopPreview() { clearTimeout(this.previewTimer); if (this.previewing) { this.previewing = null; this.speaker.stop('preview'); } }
  download(slot, it, shown) {   // the steps the clock reports while it downloads a sound
    const s = this.s, d = { slot, status: 'Downloading 0%' };
    s.download = d;
    const steps = [];
    for (let p = 10; p <= 100; p += 10) steps.push('Downloading ' + p + '%');
    steps.push('Checking ' + shown);
    for (let p = 20; p <= 100; p += 20) steps.push('Saving ' + p + '%');
    let k = 0;
    const next = () => {
      if (s.download !== d) return;
      if (k < steps.length) { d.status = steps[k++]; this.changed(); setTimeout(next, 260); return; }
      s.sound[slot] = it; d.done = true;
      const sec = this.durations && this.durations[this.catalog[slot][it].url];
      d.status = `${shown} downloaded and in use` + (sec ? ` (${Math.floor(sec / 60)}:${String(Math.round(sec % 60)).padStart(2, '0')})` : '');
      this.changed();
    };
    setTimeout(next, 200);
  }

  // ---- drawing
  draw() {
    const g = this.g;
    g.fit();
    g.clear();
    const lv = this.level();
    this.canvas.style.filter = lv.pct === 0 ? 'brightness(0)' : lv.pct < 100 ? `brightness(${0.35 + 0.65 * Math.sqrt(lv.pct / 100)})` : '';
    if (lv.pct === 0) { this.flashing = false; return; }
    const acc = PALETTES[this.s.palette].accent;
    if (this.menu.isOpen) { drawMenu(g, this.menu, acc); this.countdown(6, 210, lv.darkIn, acc); this.flashing = false; return; }
    if (this.s.setup) { this.drawSetup(acc, lv.darkIn); return; }
    this.drawFace(acc, lv.darkIn);
  }
  countdown(x, y, s, acc) {
    if (s < 0) return;
    const g = this.g, num = `${s}s`, w = 6 + 16 + 4 + g.tw('t_label', num) + 9;
    g.rrect(x, y, w, 24, 12, T.CARD); g.rframe(x, y, w, 24, 12, 2, acc);
    g.icon(x + 6, y + 4, 16, SYM.DARK_MODE, acc);
    g.print(x + 26, y + 17, 't_label', T.FG, 'l', num);
  }
  drawToast(acc) {
    const g = this.g, k = this.s.toast;
    if (!k) return;
    g.rrect(26, 58, 188, 124, 16, T.CARD); g.rframe(26, 58, 188, 124, 16, 2, acc);
    if (k === 3) {
      const on = this.s.relay;
      g.icon(106, 70, 28, SYM.POWER, on ? acc : T.SUB);
      g.print(120, 126, 't_head', T.SUB, 'c', 'Relay');
      g.print(120, 164, 't_big', on ? acc : T.FG, 'c', on ? 'ON' : 'OFF');
      return;
    }
    const pct = Math.max(0, Math.min(100, k === 2 ? this.fajrVol() : this.s.volume));
    const lab = k === 2 ? 'Fajr volume' : 'Volume', x0 = 120 - (g.tw('t_head', lab) + 28) / 2;
    g.icon(x0, 74, 22, SYM.VOLUME_UP, acc);
    g.print(x0 + 28, 92, 't_head', T.SUB, 'l', lab);
    g.print(120, 140, 't_big', T.FG, 'c', `${pct}%`);
    g.rrect(46, 154, 148, 10, 5, T.LINE);
    if (pct > 0) g.rrect(46, 154, Math.max(10, 148 * pct / 100), 10, 5, acc);
  }
  drawSetup(acc, darkIn) {
    const g = this.g, su = this.s.setup;
    g.icon(14, 10, 28, SYM.WIFI, acc);
    g.print(50, 33, 't_item', T.FG, 'l', 'Wi-Fi setup');
    g.rrect(10, 46, 220, 102, 12, T.CARD);
    g.icon(20, 56, 22, SYM.BLUETOOTH, acc);
    g.print(52, 70, 't_text', T.SUB, 'l', 'Bluetooth');
    const bt = { starting: 'Starting...', on: 'On', ask: 'Press Select', allowed: 'Allowed', joining: 'Joining...' }[su.ble];
    g.print(52, 94, 't_head', su.ble === 'ask' ? T.WARN : T.FG, 'l', bt);
    g.print(52, 116, 't_label', acc, 'l', 'improv-wifi.com');
    g.print(52, 135, 't_small', T.SUB, 'l', 'in Chrome or Edge');
    g.rrect(10, 154, 220, 80, 12, T.CARD);
    g.icon(20, 164, 22, SYM.HOTSPOT, acc);
    g.print(52, 178, 't_text', T.SUB, 'l', su.ap ? 'Hotspot: on' : 'Hotspot: soon');
    if (su.ap) {
      g.print(52, 198, 't_label', T.FG, 'l', 'AthanFallbackHotspot');
      g.print(52, 217, 't_small', T.SUB, 'l', 'password athan404');
    }
    this.countdown(170, 12, darkIn, acc);
    this.flashing = false;
  }
  drawFace(acc, darkIn) {
    const g = this.g, s = this.s, st = this.state(), p = st.p, pal = PALETTES[s.palette], ath = pal.athan;
    const fg = T.FG, sub = T.SUB, dim = T.DIM;
    const hhmmS = (h, m) => (s.clock12 ? `${h % 12 || 12}:${String(m).padStart(2, '0')}` : `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}`);
    const timeOk = st.timeOk, timesOk = st.timesOk, times = st.times;
    const athan = timeOk && !!s.athan, known = timesOk;
    const npi = st.npi, api = s.athan ? s.athan.idx : 0;
    const early = !athan && (npi === 1 || npi === 2), hi = athan ? api : early ? 3 : npi;
    const cur = st.cur;
    const nh = times ? Math.floor(times[npi] / 60) : 0, nm = times ? times[npi] % 60 : 0;
    const updateReady = s.wifi && !!s.update;

    let line = timeOk ? hhmmS(p.h, p.mi) : '--:--';
    g.print(10, 38, 't_time', fg, 'l', line);
    if (timeOk) {
      const sx = 10 + g.tw('t_time', line) + 4;
      g.print(sx, 38, 't_head', sub, 'l', String(p.s).padStart(2, '0'));
      if (s.clock12) g.print(sx, 20, 't_label', sub, 'l', p.h < 12 ? 'AM' : 'PM');
    }
    g.print(230, 20, 't_text', sub, 'r', timeOk ? `${DAYS[p.dow]} ${p.d} ${MONTHS[p.mo - 1]}` : '--- -- ---');
    {
      let x = 230; const top = 26;
      if (timesOk && (this.day.standin || s.standinForced)) { x -= 16; g.icon(x, top, 16, SYM.HISTORY, T.WARN); x -= 5; }
      if (!s.wifi) { x -= 16; g.icon(x, top, 16, SYM.WIFI_OFF, T.BAD); x -= 5; }
      if (s.locked) { x -= 16; g.icon(x, top, 16, SYM.LOCK, sub); x -= 5; }
      if (updateReady) { x -= 16; g.icon(x, top, 16, SYM.UPGRADE, acc); x -= 5; }
      if (s.radio >= 0) {
        const num = String(s.radio + 1);
        x -= g.tw('t_label', num);
        g.print(x, top + 13, 't_label', acc, 'l', num);
        x -= 18;
        g.icon(x, top, 16, SYM.RADIO, acc);
      }
    }
    const CY = 114, cx = 120, cy = CY, ro = 64, ri = 55;
    this.flashing = false;
    if (athan) {
      g.arc(cx, cy, ro, ri, 0, 1, ath);
      g.icon(106, CY - 46, 28, SYM.VOLUME_UP, ath);
      g.print(120, CY + 12, 't_item', fg, 'c', NAMES7[api]);
      g.print(120, CY + 34, 't_label', ath, 'c', 'ATHAN TIME');
    } else {
      let remS = 0, drawn = false;
      if (timesOk) {
        const nx = times[npi], nowS = st.nowS;
        remS = nx * 60 - nowS; if (remS < 0) remS += 86400;
        const prev = npi > 0 ? times[npi - 1] : times[6] - 1440;
        const cS = npi === 0 && cur > times[6] ? nowS - 86400 : nowS;
        const f = nx > prev ? Math.max(0, Math.min(1, (cS - prev * 60) / ((nx - prev) * 60))) : 0;
        const mid = (ro + ri) / 2, rc = (ro - ri) / 2, a = f * Math.PI * 2;
        const due = remS < 60 && npi !== 1 && npi !== 2 && s.athanOn[npi];
        if (due) {
          this.flashing = true;
          const lit = (this.nowMs() % 1000) < 500;
          g.arc(cx, cy, ro, ri, 0, 1, lit ? ath : T.TRACK);
        } else {
          g.arc(cx, cy, ro, ri, 0, 1, T.TRACK);
          g.arc(cx, cy, ro, ri, 0, f, acc);
          g.circle(cx, cy - mid, rc, acc);
          g.circle(cx + mid * Math.sin(a), cy - mid * Math.cos(a), rc + 2, fg);
        }
        drawn = true;
      }
      if (!drawn) g.arc(cx, cy, ro, ri, 0, 1, T.TRACK);
      const nmName = known ? NAMES7[npi] : 'Next';
      const muted = known && npi !== 1 && npi !== 2 && !s.athanOn[npi];
      const nw = g.tw('t_head', nmName), gw = nw + (muted ? 20 : 0);
      g.print(120 - gw / 2, CY - 16, 't_head', acc, 'l', nmName);
      if (muted) g.icon(120 - gw / 2 + nw + 4, CY - 31, 16, SYM.VOLUME_OFF, acc);
      const rem = Math.floor((remS + 59) / 60);
      line = !timesOk ? '--' : remS < 60 ? `${remS}s` : rem >= 60 ? `${Math.floor(rem / 60)}h ${String(rem % 60).padStart(2, '0')}m` : `${rem}m`;
      g.print(120, CY + 14, 't_big', fg, 'c', line);
      const tm = known ? hhmmS(nh, nm) : '--:--';
      line = `at ${tm}${known && s.clock12 ? (nh < 12 ? ' AM' : ' PM') : ''}`;
      g.print(120, CY + 36, 't_text', sub, 'c', line);
      if (!timesOk) { const w = g.tw('t_text', line); g.rect(120 - w / 2 - 2, CY + 30, w + 4, 2, sub); }
    }
    {
      const pad = 5, top = 200, h = 40, tms = [], w = [];
      let total = 0, hk = -1;
      for (let k = 0; k < 5; k++) {
        tms.push(known ? hhmmS(Math.floor(times[IDX5[k]] / 60), times[IDX5[k]] % 60) : '--:--');
        w.push(Math.max(g.tw('t_label', FIVE[k]), g.tw('t_num', tms[k])));
        total += w[k];
        if (IDX5[k] === hi) hk = k;
      }
      if (hk >= 0) total += 2 * pad;
      const extra = (238 - total) / 5, muteDim = blend(acc, '#000000', 0.55);
      let x = 1;
      for (let k = 0; k < 5; k++) {
        const pi = IDX5[k], nx = k === hk, cw = w[k] + (nx ? 2 * pad : 0) + extra, cxk = Math.floor(x + cw / 2);
        x += cw;
        const past = timesOk && !nx && times[pi] < cur && !(npi === 0 && cur < times[0]);
        let nameC = past ? dim : sub, timeC = past ? dim : fg;
        const bw = w[k] + 2 * pad, bx = cxk - bw / 2;
        if (nx && early) g.rframe(bx, top, bw, h, 9, 2, acc);
        else if (nx) { g.rrect(bx, top, bw, h, 9, athan ? ath : acc); nameC = timeC = T.INK; }
        g.print(cxk, 215, nx && !early ? 't_label' : 't_small', nameC, 'c', FIVE[k]);
        g.print(cxk, 234, 't_num', timeC, 'c', tms[k]);
        if (!timesOk) { const tww = g.tw('t_num', tms[k]); g.rect(cxk - tww / 2 - 2, 229, tww + 4, 2, timeC); }
        if (!s.athanOn[pi]) g.icon(cxk - 8, 182, 16, SYM.VOLUME_OFF, nx ? acc : muteDim);
      }
    }
    this.countdown(6, 44, darkIn, acc);
    this.drawToast(acc);
  }

  /** What the keys do right now, for the text beside the simulator. */
  describe() {
    const s = this.s;
    if (s.setup) return { where: 'Wi-Fi setup', keys: [['Select', s.setup.ble === 'ask' ? 'Allow the phone to connect (it asked just now)' : 'Allows a phone over Bluetooth once it asks']] };
    if (this.level().pct === 0) return { where: 'The screen is dark', keys: [['Any key', 'Lights it, dimly, for 2 minutes. That press does nothing else']] };
    if (s.locked) return { where: 'Buttons locked', keys: [['Any key', 'Only stops what is playing'], ['To unlock', 'Switch off Buttons Locked below (on the web page), or plug the clock in while holding a direction']] };
    if (this.menu.isOpen) {
      const row = this.menu.rows[this.menu.row];
      const sel = row.exit ? 'Back to the clock' : row.kind === KIND.VALUE ? 'Closes the menu' : row.kind === KIND.ACTION ? 'Does what the row shows' : 'Takes the choice shown (on the one in use, closes)';
      return {
        where: `Menu: ${row.title}`,
        keys: [['Up / Down', 'Another row (they go round)'], ['Left / Right', row.exit ? 'Back to the clock' : row.kind === KIND.VALUE ? 'Changes it at once' : 'The choices'], ['Select', sel]],
      };
    }
    if (this.soundOn) return { where: s.athan ? `${NAMES7[s.athan.idx]} athan` : 'A sound is playing', keys: [['Up, Down or Select', 'Stops it; the radio plays on'], ['Left / Right', 'Its volume']] };
    return {
      where: 'The clock screen',
      keys: [['Select', 'Opens the menu'], ['Up', s.radio >= 0 ? 'Stops the radio' : 'Radio on (the station chosen last)'], ['Down', 'Relay on or off'], ['Left / Right', 'Volume down or up']],
    };
  }

  /** Lengths of the sound files (for the athan's time on screen and the download message). */
  async measure() {
    this.durations = {};
    const urls = this.catalog.flat().map(e => e.url);
    await Promise.all(urls.map(u => new Promise(res => {
      const a = new Audio(); a.preload = 'metadata';
      a.onloadedmetadata = () => { this.durations[u] = a.duration; res(); }; a.onerror = res; a.src = 'audio/' + u;
    })));
  }
}

// ---------------------------------------------------------------- the clock's front, drawn to scale (hardware/enclosure)
// Case frame: X right, Y up, millimetres, 0 at the board's top-left on its front (enclosure.py). The SVG's y runs
// down from 0.5 mm above the crescent's tip.
const X_L = -4.49, W = 68.58, TOPY = 64.99;
const sx = X => X - X_L, sy = Y => TOPY - Y;
function star(cx, cy, r) {
  const ri = r * Math.cos(Math.PI / 4) / Math.cos(Math.PI / 8), pts = [];
  for (let i = 0; i < 16; i++) { const a = Math.PI / 8 + i * Math.PI / 8, rr = i % 2 ? ri : r; pts.push(`${(cx + rr * Math.cos(a)).toFixed(2)},${(cy + rr * Math.sin(a)).toFixed(2)}`); }
  return `<polygon points="${pts.join(' ')}"/>`;
}
const CASE_COLOURS = {
  ivory: { body: '#efe7d4', edge: '#d7ccb4', hole: '#2b2b2b', text: '#cbbf9f', frame: '#ddd2b9' },
  teal: { body: '#1f6b61', edge: '#16544c', hole: '#10211f', text: '#175a51', frame: '#1a5f56' },
  black: { body: '#26292c', edge: '#16181a', hole: '#0b0c0d', text: '#1c1e20', frame: '#1f2124' },
};
export function caseSVG(colour = 'ivory') {
  const c = CASE_COLOURS[colour] || CASE_COLOURS.ivory;
  const H = TOPY + 52.6, springY = sy(0), apexY = sy(48.89), botY = sy(-52.6), mid = sx(29.8);
  const outline = `M0 ${springY} L0 ${botY - 4} A4 4 0 0 0 4 ${botY} L${W - 4} ${botY} A4 4 0 0 0 ${W} ${botY - 4} L${W} ${springY} A52 52 0 0 0 ${mid} ${apexY} A52 52 0 0 0 0 ${springY} Z`;
  const d = 2.6, inner = `M${d} ${springY} L${d} ${botY - 4} A${4 - d / 2} ${4 - d / 2} 0 0 0 ${4 + d / 2} ${botY - d} L${W - 4 - d / 2} ${botY - d} A${4 - d / 2} ${4 - d / 2} 0 0 0 ${W - d} ${botY - 4} L${W - d} ${springY} A${52 - d} ${52 - d} 0 0 0 ${mid} ${sy(48.89 - d * 1.06)} A${52 - d} ${52 - d} 0 0 0 ${d} ${springY} Z`;
  const ya = 48.89;
  const crescent = `<rect x="${mid - 1.1}" y="${sy(ya + 3.4)}" width="2.2" height="5.4" fill="${c.body}"/><circle cx="${mid}" cy="${sy(ya + 3.8)}" r="2" fill="${c.body}"/>` +
    `<path fill-rule="evenodd" fill="${c.body}" d="M${mid - 5} ${sy(ya + 10.6)} a5 5 0 1 0 10 0 a5 5 0 1 0 -10 0 Z M${mid - 4.6} ${sy(ya + 12.2)} a4.6 4.6 0 1 0 9.2 0 a4.6 4.6 0 1 0 -9.2 0 Z"/>`;
  const spk = [29.8, 22.0];
  let grille = star(sx(spk[0]), sy(spk[1]), 3.4);
  for (const [n, rad, r] of [[8, 7.6, 2.1], [16, 13.4, 1.65]]) {
    for (let i = 0; i < n; i++) { const a = 2 * Math.PI * i / n + (n === 16 ? Math.PI / n : 0); grille += star(sx(spk[0] + rad * Math.cos(a)), sy(spk[1] + rad * Math.sin(a)), r); }
  }
  const act = { x: sx(29.8 - 13.86), y: sy(-21.01 + 13.86), w: 27.72 };
  const win = { x: act.x - 1.1, y: act.y - 1.1, w: act.w + 2.2 };
  const knob = [sx(53.0), sy(-25.75)];
  return `<svg class="case" viewBox="0 0 ${W} ${H.toFixed(2)}" role="img" aria-label="The clock's front: the speaker grille in the arch, the screen, the joystick knob">
    <defs><linearGradient id="cg-${colour}" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#fff" stop-opacity=".28"/><stop offset=".5" stop-color="#fff" stop-opacity="0"/><stop offset="1" stop-color="#000" stop-opacity=".12"/></linearGradient>
    <radialGradient id="kg" cx=".4" cy=".35" r=".7"><stop offset="0" stop-color="#f6cf6a"/><stop offset="1" stop-color="#b8861f"/></radialGradient></defs>
    ${crescent}
    <path d="${outline}" fill="${c.body}" stroke="${c.edge}" stroke-width=".5"/>
    <path d="${outline}" fill="url(#cg-${colour})"/>
    <path d="${inner}" fill="none" stroke="${c.frame}" stroke-width=".45"/>
    <g fill="${c.hole}">${grille}</g>
    <circle cx="${sx(10.4)}" cy="${sy(-4.0)}" r="1.0" fill="${c.hole}"/>
    <circle class="led" cx="${sx(10.4)}" cy="${sy(-4.0)}" r=".62" fill="#3b82f6" opacity="0"/>
    <rect x="${win.x}" y="${win.y}" width="${win.w}" height="${win.w}" rx=".8" fill="#0a0b0c" stroke="${c.edge}" stroke-width=".5"/>
    <g class="knob-hit" data-key="select"><circle cx="${knob[0]}" cy="${knob[1]}" r="5.4" fill="${c.hole}" opacity=".55"/><circle class="cap" cx="${knob[0]}" cy="${knob[1]}" r="4.7" fill="url(#kg)"/>
      <g fill="#a87914" opacity=".75">${star(knob[0], knob[1], 2.6)}</g></g>
    <circle cx="${sx(55.24)}" cy="${sy(-34.1)}" r="2.2" fill="${c.hole}"/><circle cx="${sx(55.24)}" cy="${sy(-34.1)}" r="1.25" fill="#2f8f4e"/>
    <circle cx="${sx(55.24)}" cy="${sy(-39.1)}" r="2.2" fill="${c.hole}"/><circle cx="${sx(55.24)}" cy="${sy(-39.1)}" r="1.25" fill="#2f8f4e"/>
    <text x="${mid}" y="${sy(-42.385) + 2.6}" text-anchor="middle" font-family="'Reem Kufi', 'Geeza Pro', 'Arial', sans-serif" font-weight="700" font-size="8" fill="${c.text}" stroke="${c.edge}" stroke-width=".18" direction="rtl" lang="ar">حي على الصلاة</text>
  </svg>`;
}
/** The screen's place in caseSVG(), as fractions of the drawing (for positioning the canvas over it). */
export const SCREEN_BOX = (() => {
  const H = TOPY + 52.6;
  return { left: sx(29.8 - 13.86) / W, top: sy(-21.01 + 13.86) / H, width: 27.72 / W, height: 27.72 / H, aspect: W / H };
})();
/** Builds the clock's front with a live screen in `host`; returns the canvas. */
export function mountDevice(host, colour = 'ivory') {
  host.classList.add('device');
  host.innerHTML = caseSVG(colour) + '<canvas class="tft" width="240" height="240" aria-label="The clock\'s screen"></canvas>';
  const cv = host.querySelector('canvas');
  Object.assign(cv.style, { left: SCREEN_BOX.left * 100 + '%', top: SCREEN_BOX.top * 100 + '%', width: SCREEN_BOX.width * 100 + '%', height: SCREEN_BOX.height * 100 + '%' });
  return cv;
}
