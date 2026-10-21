// Shared by every page of the website (GitHub Pages from docs/): the header and footer, the theme, links into the
// repository, the clocks' own data (docs/: the mosques, their yearly times, the sound list, the stations) and the
// time-zone arithmetic the yearly files need (they carry a POSIX TZ string, as the clock does).

export const REPO = 'et7ad/esp32_athan';
export const BRANCH = 'main';
export const GITHUB = `https://github.com/${REPO}`;

// Files outside docs/ (pictures, the case's STL files, the guides) are not part of the website: on GitHub Pages they
// come from raw.githubusercontent.com. A local preview served from the repository's root (python3 -m http.server,
// then /docs/) reads them from the working copy instead, so new pictures show before they are pushed.
const LOCAL_REPO = location.pathname.includes('/docs/');
export function repo(path) {
  return LOCAL_REPO ? `../${path}` : `https://raw.githubusercontent.com/${REPO}/${BRANCH}/${path}`;
}
export const blob = path => `${GITHUB}/blob/${BRANCH}/${path}`;
export const tree = path => `${GITHUB}/tree/${BRANCH}/${path}`;

// ---------------------------------------------------------------- storage (per-browser conveniences only)
export function remember(key, value) {
  try { if (value === undefined) return localStorage.getItem(key); localStorage.setItem(key, value); } catch (e) { /* blocked */ }
  return null;
}

// ---------------------------------------------------------------- header, footer, theme
const PAGES = [
  ['setup.html', 'Set up', 'Connect a new clock to Wi-Fi and choose your mosque'],
  ['use.html', 'Use', 'The screen, the joystick, the menu and the web page'],
  ['sounds.html', 'Sounds', 'Listen to the athans, tawashih and radio stations'],
  ['times.html', 'Prayer times', 'The mosques and their yearly timetables'],
  ['build.html', 'Build', 'Parts, wiring, firmware, the board and the case'],
  ['read.html', 'Guides', 'The full written guides'],
];

const BRAND_SVG = `<svg viewBox="0 0 26 34" aria-hidden="true"><path d="M2 32V17.5A11 11 0 0 1 13 4a11 11 0 0 1 11 13.5V32z" fill="none" stroke="var(--teal)" stroke-width="2.2" stroke-linejoin="round"/><path d="M13 1.2v2.6" stroke="var(--teal)" stroke-width="2" stroke-linecap="round"/><path d="m13 14.2 1.3 2.4 2.6-.5-.5 2.6 2.4 1.3-2.4 1.3.5 2.6-2.6-.5-1.3 2.4-1.3-2.4-2.6.5.5-2.6-2.4-1.3 2.4-1.3-.5-2.6 2.6.5z" fill="var(--gold)"/></svg>`;
const GH_SVG = `<svg viewBox="0 0 16 16" aria-hidden="true"><path fill="currentColor" d="M8 0C3.58 0 0 3.58 0 8a8 8 0 0 0 5.47 7.59c.4.07.55-.17.55-.38v-1.33c-2.23.48-2.7-1.07-2.7-1.07-.36-.92-.89-1.17-.89-1.17-.73-.5.06-.49.06-.49.8.06 1.23.83 1.23.83.72 1.22 1.88.87 2.33.66.07-.52.28-.87.5-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82a7.6 7.6 0 0 1 4 0c1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48v2.2c0 .21.15.46.55.38A8 8 0 0 0 16 8c0-4.42-3.58-8-8-8z"/></svg>`;

function effectiveTheme() {
  const t = document.documentElement.dataset.theme;
  if (t === 'light' || t === 'dark') return t;
  return matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
}

export function renderChrome() {
  const here = location.pathname.split('/').pop() || 'index.html';
  const header = document.getElementById('site-header');
  if (header) {
    header.className = 'site-header';
    header.innerHTML = `<div class="wrap">
      <a class="brand" href="index.html" aria-label="Athan clock, home">${BRAND_SVG}<b>Athan clock</b></a>
      <nav class="nav" id="nav" aria-label="Main">${PAGES.map(([href, label]) =>
        `<a href="${href}"${href === here ? ' aria-current="page"' : ''}>${label}</a>`).join('')}</nav>
      <div class="header-tools">
        <button class="icon-btn" id="theme-btn" type="button" aria-label="Switch between light and dark"><span class="icon">contrast</span></button>
        <a class="icon-btn" href="${GITHUB}" aria-label="The project on GitHub">${GH_SVG}</a>
        <button class="icon-btn menu-btn" id="menu-btn" type="button" aria-label="Menu" aria-expanded="false" aria-controls="nav"><span class="icon">menu</span></button>
      </div></div>`;
    const nav = header.querySelector('#nav'), mb = header.querySelector('#menu-btn');
    mb.addEventListener('click', () => {
      const open = nav.classList.toggle('open');
      mb.setAttribute('aria-expanded', String(open));
      mb.firstElementChild.textContent = open ? 'close' : 'menu';
    });
    header.querySelector('#theme-btn').addEventListener('click', () => {
      const next = effectiveTheme() === 'dark' ? 'light' : 'dark';
      document.documentElement.dataset.theme = next;
      remember('athan-theme', next);
      document.dispatchEvent(new CustomEvent('themechange'));
    });
  }
  const footer = document.getElementById('site-footer');
  if (footer) {
    footer.className = 'site-footer';
    footer.innerHTML = `<div class="wrap">
      <div><span class="ar" lang="ar">حي على الصلاة</span><b>Athan clock</b>
        <p>An open prayer clock: an ESP32-S3, a small amplifier and a screen, with each mosque's times published here
        on GitHub. Older clocks (version 2, ESP8266) live in <a style="display:inline" href="https://github.com/et7ad/esp_athan">et7ad/esp_athan</a>.</p></div>
      <div><b>Owners</b><a href="setup.html">Set up a clock</a><a href="use.html">Use it</a><a href="sounds.html">Sounds and radio</a><a href="times.html">Prayer times</a></div>
      <div><b>Builders</b><a href="build.html#parts">Parts list</a><a href="build.html#prototype">Prototype</a><a href="build.html#firmware">Firmware</a><a href="build.html#case">The case</a></div>
      <div><b>Project</b><a href="read.html">Guides</a><a href="${GITHUB}">Source on GitHub</a><a href="${GITHUB}/issues">Ask for your mosque</a><a href="${blob('LICENSE')}">License</a></div>
    </div>`;
  }
}

// ---------------------------------------------------------------- small page helpers
export function wireCommon() {
  document.querySelectorAll('img[data-repo]').forEach(img => { img.src = repo(img.dataset.repo); });
  document.querySelectorAll('a[data-repo]').forEach(a => { a.href = repo(a.dataset.repo); });
  document.querySelectorAll('a[data-blob]').forEach(a => { a.href = blob(a.dataset.blob); });
  document.querySelectorAll('a[data-tree]').forEach(a => { a.href = tree(a.dataset.tree); });
  // a download that keeps its file name (raw GitHub files would open as text in the browser)
  document.querySelectorAll('a[data-save]').forEach(a => {
    a.href = repo(a.dataset.save);
    a.addEventListener('click', async ev => {
      ev.preventDefault();
      try {
        const r = await fetch(repo(a.dataset.save));
        if (!r.ok) throw new Error(r.status);
        saveBlob(await r.blob(), a.dataset.save.split('/').pop());
      } catch (e) { location.href = repo(a.dataset.save); }
    });
  });
  document.querySelectorAll('.code').forEach(box => {
    if (box.querySelector('button')) return;
    const b = document.createElement('button');
    b.type = 'button'; b.setAttribute('aria-label', 'Copy');
    b.innerHTML = '<span class="icon">content_copy</span>';
    b.addEventListener('click', async () => {
      const text = box.querySelector('pre').innerText.replace(/\n$/, '');
      try { await navigator.clipboard.writeText(text); b.innerHTML = '<span class="icon">check</span>'; }
      catch (e) { b.innerHTML = '<span class="icon">close</span>'; }
      setTimeout(() => { b.innerHTML = '<span class="icon">content_copy</span>'; }, 1400);
    });
    box.appendChild(b);
  });
}

export function saveBlob(data, name) {
  const url = URL.createObjectURL(data instanceof Blob ? data : new Blob([data]));
  const a = Object.assign(document.createElement('a'), { href: url, download: name });
  document.body.appendChild(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 2000);
}

// Highlights the side navigation's link for the chapter on screen.
export function scrollSpy(tocSel = '.toc') {
  const toc = document.querySelector(tocSel);
  if (!toc) return;
  const links = [...toc.querySelectorAll('a[href^="#"]')];
  const map = new Map(links.map(a => [a.getAttribute('href').slice(1), a]));
  const seen = new Set();
  const io = new IntersectionObserver(entries => {
    entries.forEach(e => { if (e.isIntersecting) seen.add(e.target.id); else seen.delete(e.target.id); });
    const first = [...map.keys()].find(id => seen.has(id));
    if (first) links.forEach(a => a.classList.toggle('on', a === map.get(first)));
  }, { rootMargin: '-20% 0px -60% 0px' });
  map.forEach((_, id) => { const el = document.getElementById(id); if (el) io.observe(el); });
}

export const esc = s => String(s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

// ---------------------------------------------------------------- the clocks' data (docs/)
const cache = new Map();
export function getJSON(url) {
  if (!cache.has(url)) cache.set(url, fetch(url).then(r => (r.ok ? r.json() : null)).catch(() => null));
  return cache.get(url);
}

export const NAMES7 = ['Fajr', 'Sunrise', 'Doha', 'Dhuhr', 'Asr', 'Maghrib', 'Isha'];
export const FIELDS7 = ['fajr', 'sunrise', 'doha', 'dhuhar', 'asr', 'maghrib', 'isha'];
export const IQAMA = { fajr: 'fajr_iqa', dhuhar: 'dhuhar_iqa', asr: 'asr_iqa', maghrib: 'maghrib_iqa', isha: 'isha_iqa' };
export const COLOR_VARS = ['--p-fajr', '--p-sunrise', '--p-doha', '--p-dhuhr', '--p-asr', '--p-maghrib', '--p-isha'];

/** The mosques the clocks offer, in the file's order, numbered as on the clock ("1. Islamic Center of Davis"). */
export async function loadMosques() {
  const j = await getJSON('athantimes/mosques.json');
  if (!j || !j.mosques) return [];
  const out = [];
  for (const [key, m] of Object.entries(j.mosques)) {
    if (m.active === false) continue;
    out.push({ key, name: m.name || key, lat: m.lat, lon: m.lon, tz: m.tz, number: out.length + 1, label: `${out.length + 1}. ${m.name || key}` });
  }
  return out;
}
export const DEFAULT_MOSQUE = 'davis';   // a new clock's (the selected_location_key global in firmware/athan.yaml)
export function chosenMosque(list) {
  const k = remember('athan-mosque');
  return list.find(m => m.key === k) || list.find(m => m.key === DEFAULT_MOSQUE) || list[0];
}

export const loadYear = (key, year) => getJSON(`athantimes/${key}/${year}.json`);
/** The years published for a mosque (the clock looks for this year, next year in December, last year as a stand-in). */
export async function yearsOf(key, around = new Date().getFullYear()) {
  const ys = [];
  for (let y = around - 1; y <= around + 1; y++) ys.push(y);   // a missing year is a quiet 404
  const got = await Promise.all(ys.map(y => loadYear(key, y)));
  return ys.filter((_, i) => got[i]);
}

/** Minutes of the day for one row of a yearly file, by field name. */
export function rowMinutes(file, dayIndex) {
  const row = file.days[dayIndex];
  if (!row) return null;
  const out = {};
  file.fields.forEach((f, i) => { const [h, m] = row[i].split(':').map(Number); out[f] = h * 60 + m; });
  return out;
}

// ---------------------------------------------------------------- POSIX time zones ("PST8PDT,M3.2.0,M11.1.0")
export function parseTZ(s) {
  let i = 0;
  const name = () => {
    if (s[i] === '<') { const j = s.indexOf('>', i); const n = s.slice(i + 1, j); i = j + 1; return n; }
    const m = /^[A-Za-z]{3,}/.exec(s.slice(i)); if (!m) return null; i += m[0].length; return m[0];
  };
  const offset = () => {   // POSIX: hours west of UTC, so the sign is flipped for "east"
    const m = /^([+-]?)(\d{1,2})(?::(\d{1,2}))?(?::(\d{1,2}))?/.exec(s.slice(i));
    if (!m) return null;
    i += m[0].length;
    const v = (+m[2]) * 3600 + (+(m[3] || 0)) * 60 + (+(m[4] || 0));
    return m[1] === '-' ? v : -v;
  };
  const rule = () => {
    let r;
    let m = /^M(\d{1,2})\.(\d)\.(\d)/.exec(s.slice(i));
    if (m) { r = { kind: 'M', m: +m[1], w: +m[2], d: +m[3] }; }
    else if ((m = /^J(\d{1,3})/.exec(s.slice(i)))) r = { kind: 'J', n: +m[1] };
    else if ((m = /^(\d{1,3})/.exec(s.slice(i)))) r = { kind: 'N', n: +m[1] };
    else return null;
    i += m[0].length;
    r.t = 7200;
    const t = /^\/([+-]?)(\d{1,3})(?::(\d{1,2}))?(?::(\d{1,2}))?/.exec(s.slice(i));
    if (t) { i += t[0].length; r.t = (t[1] === '-' ? -1 : 1) * ((+t[2]) * 3600 + (+(t[3] || 0)) * 60 + (+(t[4] || 0))); }
    return r;
  };
  const tz = { std: name(), stdOff: offset() };
  if (tz.std === null || tz.stdOff === null) return { stdOff: 0 };
  if (i < s.length && s[i] !== ',') {
    tz.dst = name();
    tz.dstOff = (i < s.length && s[i] !== ',') ? offset() : tz.stdOff + 3600;
    if (s[i] === ',') { i++; tz.start = rule(); if (s[i] === ',') { i++; tz.end = rule(); } }
    if (!tz.start || !tz.end) {   // no rules given: the US ones, as tz_posix.cpp does
      tz.start = { kind: 'M', m: 3, w: 2, d: 0, t: 7200 };
      tz.end = { kind: 'M', m: 11, w: 1, d: 0, t: 7200 };
    }
  }
  return tz;
}
function ruleDayMs(r, year) {   // midnight (UTC-based civil) of the rule's day in that year
  if (r.kind === 'M') {
    const first = Date.UTC(year, r.m - 1, 1), dow = new Date(first).getUTCDay();
    let day = 1 + ((r.d - dow + 7) % 7) + (r.w - 1) * 7;
    const dim = new Date(Date.UTC(year, r.m, 0)).getUTCDate();
    while (day > dim) day -= 7;
    return Date.UTC(year, r.m - 1, day);
  }
  if (r.kind === 'J') {   // 1..365, February 29 never counted
    const leap = (year % 4 === 0 && year % 100 !== 0) || year % 400 === 0;
    const n = r.n + (leap && r.n >= 60 ? 1 : 0);
    return Date.UTC(year, 0, 1) + (n - 1) * 86400000;
  }
  return Date.UTC(year, 0, 1) + r.n * 86400000;
}
/** Seconds east of UTC at the instant utcMs. */
export function offsetAt(tz, utcMs) {
  if (!tz.dst) return tz.stdOff;
  const year = new Date(utcMs + tz.stdOff * 1000).getUTCFullYear();
  const start = ruleDayMs(tz.start, year) + tz.start.t * 1000 - tz.stdOff * 1000;
  const end = ruleDayMs(tz.end, year) + tz.end.t * 1000 - tz.dstOff * 1000;
  const inDst = start < end ? utcMs >= start && utcMs < end : !(utcMs >= end && utcMs < start);
  return inDst ? tz.dstOff : tz.stdOff;
}
/** The civil time at a place: year, month (1-12), day, hour, minute, second, weekday (0 = Sunday), day of year. */
export function localParts(tz, utcMs) {
  const d = new Date(utcMs + offsetAt(tz, utcMs) * 1000);
  const y = d.getUTCFullYear();
  return {
    y, mo: d.getUTCMonth() + 1, d: d.getUTCDate(), h: d.getUTCHours(), mi: d.getUTCMinutes(), s: d.getUTCSeconds(),
    ms: d.getUTCMilliseconds(), dow: d.getUTCDay(), doy: Math.floor((Date.UTC(y, d.getUTCMonth(), d.getUTCDate()) - Date.UTC(y, 0, 1)) / 86400000) + 1,
  };
}

/** A mosque's times for its civil date at utcMs, as the clock works them out: this year's file, or the same date
 *  of last year converted through UTC (the stand-in, shown with a mark) when this year is not published yet. */
export async function dayTimes(mosque, utcMs) {
  const tz = parseTZ(mosque.tz || 'UTC0');
  const p = localParts(tz, utcMs);
  let file = await loadYear(mosque.key, p.y), standin = false, mins = null;
  if (file) mins = rowMinutes(file, p.doy - 1);
  if (!mins) {
    const prev = await loadYear(mosque.key, p.y - 1);
    if (!prev) return { parts: p, tz, times: null, standin: false };
    const day = (p.mo === 2 && p.d === 29) ? 28 : p.d;
    const prevMidUtc = Date.UTC(p.y - 1, p.mo - 1, day, 12) - tz.stdOff * 1000;
    const pp = localParts(tz, prevMidUtc);
    const raw = rowMinutes(prev, pp.doy - 1);
    const shift = (offsetAt(tz, utcMs) - offsetAt(tz, prevMidUtc)) / 60;
    mins = {};
    for (const k in raw) mins[k] = ((raw[k] + shift) % 1440 + 1440) % 1440;
    file = prev; standin = true;
  }
  return { parts: p, tz, times: FIELDS7.map(f => mins[f]), iqama: mins, standin, year: file.year };
}

/** "05:57" or, with the 12-hour clock, "5:57" (AM/PM given separately where the clock shows it). */
export function hhmm(min, h12 = false) {
  const h = Math.floor(min / 60) % 24, m = min % 60;
  return h12 ? `${h % 12 || 12}:${String(m).padStart(2, '0')}` : `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}`;
}
export const ampm = min => (Math.floor(min / 60) % 24 < 12 ? 'AM' : 'PM');
export function cssVar(name) { return getComputedStyle(document.documentElement).getPropertyValue(name).trim(); }

// ---------------------------------------------------------------- every page
renderChrome();
if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', wireCommon); else wireCommon();
