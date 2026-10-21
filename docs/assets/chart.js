// The year of a mosque's prayer times as seven lines (docs/athantimes/<key>/<year>.json), with a crosshair that
// reads out every time of the day under the pointer. Drawn as SVG; the colours are site.css's --p-* (validated for
// colour-blind readers on both themes), each line also labelled at its end.

import { NAMES7, FIELDS7, COLOR_VARS, rowMinutes, hhmm, cssVar, esc } from './site.js';

const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
const NS = 'http://www.w3.org/2000/svg';

export class YearChart {
  constructor(host, legend) {
    this.host = host;
    this.legend = legend;
    this.shown = new Set([0, 1, 2, 3, 4, 5, 6]);
    this.tip = document.createElement('div');
    this.tip.className = 'chart-tip';
    this.tip.setAttribute('role', 'status');
    host.appendChild(this.tip);
    document.addEventListener('themechange', () => this.file && this.render());
    matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => this.file && this.render());
    if (legend) this.renderLegend();
  }
  renderLegend() {
    this.legend.innerHTML = NAMES7.map((n, i) =>
      `<button type="button" data-i="${i}" aria-pressed="${this.shown.has(i)}" style="--c:var(${COLOR_VARS[i]})"><i></i>${n}</button>`).join('');
    this.legend.onclick = ev => {
      const b = ev.target.closest('[data-i]'); if (!b) return;
      const i = +b.dataset.i;
      if (this.shown.has(i) && this.shown.size > 1) this.shown.delete(i); else this.shown.add(i);
      this.renderLegend(); this.render();
    };
  }
  /** file: a yearly file; today: the day index to mark (or -1) */
  set(file, today = -1, h12 = false) { this.file = file; this.today = today; this.h12 = h12; this.render(); }

  render() {
    const f = this.file, days = f.days.length;
    this.rows = Array.from({ length: days }, (_, d) => rowMinutes(f, d));
    const W = 1100, H = 470, L = 56, R = 86, T = 16, B = 34;
    const all = this.rows.flatMap(r => FIELDS7.map(k => r[k]));
    const lo = Math.floor((Math.min(...all) - 20) / 60) * 60, hi = Math.ceil((Math.max(...all) + 20) / 60) * 60;
    const x = d => L + (d / (days - 1)) * (W - L - R), y = m => T + (1 - (m - lo) / (hi - lo)) * (H - T - B);
    this.geom = { W, H, L, R, T, B, x, y, days };
    const svg = document.createElementNS(NS, 'svg');
    svg.setAttribute('viewBox', `0 0 ${W} ${H}`);
    svg.setAttribute('role', 'img');
    svg.setAttribute('aria-label', `Prayer times through ${f.year}: ${NAMES7.join(', ')}, one line each`);
    let grid = '', axis = '';
    for (let m = lo; m <= hi; m += 60) {
      grid += `<line x1="${L}" x2="${W - R}" y1="${y(m)}" y2="${y(m)}"/>`;
      axis += `<text x="${L - 10}" y="${y(m) + 4}" text-anchor="end">${hhmm(m, this.h12)}${this.h12 ? (Math.floor(m / 60) % 24 < 12 ? ' am' : ' pm') : ''}</text>`;
    }
    const leap = days === 366;
    let first = 0;
    MONTHS.forEach((mo, i) => {
      const len = [31, leap ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31][i];
      grid += `<line x1="${x(first)}" x2="${x(first)}" y1="${T}" y2="${H - B}" style="opacity:.6"/>`;
      axis += `<text x="${x(first + len / 2)}" y="${H - 10}" text-anchor="middle">${mo}</text>`;
      first += len;
    });
    let series = '', labels = '';
    const ends = [];
    FIELDS7.forEach((k, i) => {
      if (!this.shown.has(i)) return;
      const pts = this.rows.map((r, d) => `${x(d).toFixed(1)},${y(r[k]).toFixed(1)}`);
      series += `<path d="M${pts.join('L')}" stroke="${cssVar(COLOR_VARS[i])}"/>`;
      ends.push({ i, yy: y(this.rows[days - 1][k]) });
    });
    // labels at the right end, spread apart where lines end close together
    ends.sort((a, b) => a.yy - b.yy);
    for (let j = 1; j < ends.length; j++) if (ends[j].yy - ends[j - 1].yy < 15) ends[j].yy = ends[j - 1].yy + 15;
    ends.forEach(e => { labels += `<text x="${W - R + 8}" y="${e.yy + 4}" style="font:700 13px var(--body);fill:var(--ink)"><tspan style="fill:${cssVar(COLOR_VARS[e.i])}">●</tspan> ${NAMES7[e.i]}</text>`; });
    const today = this.today >= 0 && this.today < days
      ? `<line x1="${x(this.today)}" x2="${x(this.today)}" y1="${T}" y2="${H - B}" stroke="var(--ink)" stroke-dasharray="4 4" stroke-width="1.5"/><text x="${x(this.today) + 6}" y="${T + 12}" style="font:700 12px var(--body);fill:var(--ink)">today</text>` : '';
    svg.innerHTML = `<g class="grid">${grid}</g><g class="axis">${axis}</g>${today}<g class="series">${series}</g>${labels}
      <g class="cross" style="display:none"><line y1="${T}" y2="${H - B}" stroke="var(--ink-2)" stroke-width="1"/><g class="dots"></g></g>
      <rect x="${L}" y="${T}" width="${W - L - R}" height="${H - T - B}" fill="transparent" class="hit"/>`;
    const old = this.host.querySelector('svg');
    if (old) old.replaceWith(svg); else this.host.insertBefore(svg, this.tip);
    this.svg = svg;
    const hit = svg.querySelector('.hit');
    const move = ev => this.hover(ev);
    hit.addEventListener('pointermove', move);
    hit.addEventListener('pointerdown', move);
    hit.addEventListener('pointerleave', () => this.hide());
    svg.tabIndex = 0;
    svg.addEventListener('keydown', ev => {
      if (ev.key !== 'ArrowLeft' && ev.key !== 'ArrowRight') return;
      ev.preventDefault();
      this.kd = Math.max(0, Math.min(days - 1, (this.kd ?? Math.max(0, this.today)) + (ev.key === 'ArrowRight' ? 1 : -1)));
      this.show(this.kd);
    });
  }
  hover(ev) {
    const r = this.svg.getBoundingClientRect(), g = this.geom;
    const vx = (ev.clientX - r.left) / r.width * g.W;
    const d = Math.round((vx - g.L) / (g.W - g.L - g.R) * (g.days - 1));
    this.show(Math.max(0, Math.min(g.days - 1, d)));
  }
  show(d) {
    const g = this.geom, row = this.rows[d], cross = this.svg.querySelector('.cross');
    cross.style.display = '';
    cross.querySelector('line').setAttribute('x1', g.x(d)); cross.querySelector('line').setAttribute('x2', g.x(d));
    cross.querySelector('.dots').innerHTML = FIELDS7.map((k, i) => (this.shown.has(i)
      ? `<circle cx="${g.x(d)}" cy="${g.y(row[k])}" r="5" fill="${cssVar(COLOR_VARS[i])}" stroke="var(--card)" stroke-width="2"/>` : '')).join('');
    const date = new Date(Date.UTC(this.file.year, 0, 1 + d));
    const label = date.toLocaleDateString(undefined, { weekday: 'short', day: 'numeric', month: 'long', timeZone: 'UTC' });
    this.tip.innerHTML = `<b>${esc(label)}</b>` + FIELDS7.map((k, i) =>
      `<div class="r"><i style="background:${cssVar(COLOR_VARS[i])}"></i><span>${NAMES7[i]}</span><span>${hhmm(row[k], this.h12)}</span></div>`).join('');
    this.tip.style.display = 'block';
    const r = this.svg.getBoundingClientRect(), hostR = this.host.getBoundingClientRect();
    const px = (g.x(d) / g.W) * r.width + (r.left - hostR.left);
    const tw = this.tip.offsetWidth;
    this.tip.style.left = (px + 16 + tw > hostR.width ? px - tw - 16 : px + 16) + 'px';
    this.tip.style.top = '12px';
  }
  hide() { this.tip.style.display = 'none'; const c = this.svg && this.svg.querySelector('.cross'); if (c) c.style.display = 'none'; }
}
