#include "menu_view.h"

#include "menu_frame.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace esphome {
namespace athan {

using display::Display;
using esphome::Color;
using display::TextAlign;

// ---- what the menu shows, for both displays

Frame frame_of(Menu &menu) {
  menu.refresh();
  const MenuRow &row = menu.row_at(menu.row());
  Frame fr;
  fr.row = menu.row();
  fr.title = row.title;
  fr.kind = row.kind;
  fr.style = row.style;
  fr.exit = row.exit;
  fr.item = menu.item();
  fr.n = menu.count();
  if (fr.item < 0) {
    fr.cap = row.caption ? row.caption(-1) : std::string();  // why there is nothing to show
    return fr;
  }
  fr.text = row.label ? row.label(fr.item) : std::string();
  fr.cap = row.caption ? row.caption(fr.item) : std::string();
  fr.active = row.active ? row.active() : -1;
  fr.in_use = fr.active == fr.item;
  fr.playing = row.playing && row.playing() == fr.item;
  if (fr.n <= MAX_SIDE_BY_SIDE) {
    if (fr.style == RowStyle::CHOICES) {
      for (int i = 0; i < fr.n; i++)
        fr.all.push_back(row.label(i));
    } else if (fr.style == RowStyle::TOGGLES && row.short_label && row.on) {
      for (int i = 0; i < fr.n; i++) {
        fr.shorts.push_back(row.short_label(i));
        fr.ons.push_back(row.on(i));
      }
    } else if (fr.style == RowStyle::SWATCHES && row.swatch) {
      for (int i = 0; i < fr.n; i++)
        fr.swatches.push_back(row.swatch(i));
    }
  }
  fr.has_left = menu.item_left() >= 0;
  fr.has_right = menu.item_right() >= 0;
  return fr;
}

static uint32_t fingerprint(const Frame &fr) {
  uint32_t h = 2166136261u;  // FNV-1a
  auto mix = [&h](const char *p, size_t n) {
    for (size_t i = 0; i < n; i++)
      h = (h ^ static_cast<uint8_t>(p[i])) * 16777619u;
    h = (h ^ 0xFFu) * 16777619u;  // separator
  };
  auto str = [&mix](const std::string &s) { mix(s.data(), s.size()); };
  mix(fr.title, std::strlen(fr.title));
  str(fr.text);
  str(fr.cap);
  for (const auto &a : fr.all)
    str(a);
  for (size_t i = 0; i < fr.shorts.size(); i++) {
    str(fr.shorts[i]);
    const char on = fr.ons[i] ? 1 : 0;
    mix(&on, 1);
  }
  for (uint32_t c : fr.swatches)
    mix(reinterpret_cast<const char *>(&c), sizeof(c));
  const int32_t nums[8] = {fr.row, fr.item, fr.n, fr.in_use, fr.playing, fr.active, fr.has_left, fr.has_right};
  mix(reinterpret_cast<const char *>(nums), sizeof(nums));
  return h;
}

static uint32_t drawn_fingerprint = 0;  // of the last menu screen sent to the display

void frame_drawn(const Frame &fr) { drawn_fingerprint = fingerprint(fr); }

bool menu_changed(Menu &menu) { return menu.is_open() && fingerprint(frame_of(menu)) != drawn_fingerprint; }

// ---- text, for both displays (menu_view.h)

int width_of(Display &d, display::BaseFont *f, const std::string &s) {
  int x1, y1, w, h;
  d.get_text_bounds(0, 0, s.c_str(), f, TextAlign::TOP_LEFT, &x1, &y1, &w, &h);
  return w;
}

// Takes the last character off t (a whole UTF-8 sequence), and the spaces before it.
static void drop_last_char(std::string &t) {
  while (!t.empty()) {
    const auto c = static_cast<unsigned char>(t.back());
    t.pop_back();
    if ((c & 0xC0) != 0x80)
      break;  // ASCII, or the first byte of a sequence: one character gone
  }
  while (!t.empty() && t.back() == ' ')
    t.pop_back();
}

std::string fit_text(Display &d, display::BaseFont *f, const std::string &s, int w, const char *ellipsis) {
  if (width_of(d, f, s) <= w)
    return s;
  std::string t = s;
  while (!t.empty()) {
    drop_last_char(t);
    if (width_of(d, f, t + ellipsis) <= w)
      return t + ellipsis;
  }
  return width_of(d, f, ellipsis) <= w ? std::string(ellipsis) : std::string();
}

std::vector<std::string> wrap_words(Display &d, display::BaseFont *f, const std::string &s, int w, size_t max_lines,
                                    const char *ellipsis) {
  std::vector<std::string> out;
  std::string line;
  size_t i = 0;
  while (i <= s.size()) {
    size_t j = s.find(' ', i);
    if (j == std::string::npos)
      j = s.size();
    const std::string word = fit_text(d, f, s.substr(i, j - i), w, ellipsis);
    const std::string t = line.empty() ? word : line + " " + word;
    if (line.empty() || width_of(d, f, t) <= w) {
      line = t;
    } else {
      out.push_back(line);
      line = word;
    }
    i = j + 1;
  }
  if (!line.empty())
    out.push_back(line);
  if (out.size() > max_lines) {  // more than fits: the last line kept ends in the ellipsis
    out.resize(max_lines);
    if (!out.empty())
      out.back() = fit_text(d, f, out.back() + ellipsis, w, ellipsis);
  }
  return out;
}

bool only_digits_and_dots(const std::string &s) {
  return !s.empty() && s.find_first_not_of("0123456789.") == std::string::npos;
}

ItemLines item_lines(Display &d, display::BaseFont *const fonts[3], const std::string &s, int w,
                     const char *ellipsis) {
  ItemLines out;
  for (int k = 0; k < 3; k++) {
    out.font = k;
    if (width_of(d, fonts[k], s) <= w) {
      out.line[0] = s;
      return out;
    }
    if (k == 0)
      continue;  // the large font: one line only
    const size_t sp = item_break(s, [&](const std::string &l) { return width_of(d, fonts[k], l); }, w);
    if (sp != std::string::npos) {
      out.count = 2;
      out.line[0] = s.substr(0, sp);
      out.line[1] = s.substr(sp + 1);
      return out;
    }
  }
  // Too long for two lines of the small font (or a word wider than a line): as much as fits, cut short.
  const auto lines = wrap_words(d, fonts[2], s, w, 2, ellipsis);
  out.count = lines.size() > 1 ? 2 : 1;
  for (size_t i = 0; i < lines.size(); i++)
    out.line[i] = lines[i];
  return out;
}

// ---- the OLED's drawing (the TFT's is menu_view_tft.cpp)
#ifndef ATHAN_DISPLAY_TFT

namespace {

// Two panes: the rows by short name on the left (five at a time, a ring, the one on screen in the middle), a line,
// then on the right the row's full title (small) and its item. Right pane, top to bottom (y of text baselines): the
// title at 6, the item around y 23, its marks at 34, dots at 49, the caption at 61 (two lines: 53 and 62).
const int W = 128;
const int LW = 54;                             // left pane width
const int PX = LW + 3, PW = W - PX;            // right pane: x 57..127
const int PC = PX + PW / 2;                    // its centre
const int ROW_H = 13, VISIBLE = 5, MIDDLE = 2;  // left pane: 5 rows of 13 px, the one on screen 3rd
const int CAP_L = 14, CAP_M = 10, CAP_S = 5;   // capital heights of the large, medium and small fonts
// The item's room: the pane but the arrows (x 58-61 and 123-126), 1 px clear of them.
const int ITEM_W = PW - 13;
// Text cut short ends in three dots: the pixel font has no ellipsis glyph.
const char *const ELLIPSIS = "...";

// A filled box with corners cut by r (1 or 2) pixels; `top_only` keeps the bottom corners square.
void rounded_fill(Display &d, int x, int y, int w, int h, int r, bool top_only = false) {
  d.filled_rectangle(x, y, w, h);
  auto cut = [&](int cx, int cy, int dx, int dy) {
    d.draw_pixel_at(cx, cy, display::COLOR_OFF);
    if (r > 1) {
      d.draw_pixel_at(cx + dx, cy, display::COLOR_OFF);
      d.draw_pixel_at(cx, cy + dy, display::COLOR_OFF);
    }
  };
  cut(x, y, 1, 1);
  cut(x + w - 1, y, -1, 1);
  if (!top_only) {
    cut(x, y + h - 1, 1, -1);
    cut(x + w - 1, y + h - 1, -1, -1);
  }
}

// An outline with corners cut by 2 pixels.
void rounded_frame(Display &d, int x, int y, int w, int h, Color c = display::COLOR_ON) {
  d.horizontal_line(x + 2, y, w - 4, c);
  d.horizontal_line(x + 2, y + h - 1, w - 4, c);
  d.vertical_line(x, y + 2, h - 4, c);
  d.vertical_line(x + w - 1, y + 2, h - 4, c);
  d.draw_pixel_at(x + 1, y + 1, c);
  d.draw_pixel_at(x + w - 2, y + 1, c);
  d.draw_pixel_at(x + 1, y + h - 2, c);
  d.draw_pixel_at(x + w - 2, y + h - 2, c);
}

// Arrows h pixels tall (odd), (h + 1) / 2 wide: the tip at x (left) or at the right edge (right).
void arrow_left(Display &d, int x, int y, int h, Color c = display::COLOR_ON) {
  const int k = h / 2;
  for (int i = 0; i <= k; i++)
    d.vertical_line(x + i, y + k - i, 2 * i + 1, c);
}
void arrow_right(Display &d, int x, int y, int h, Color c = display::COLOR_ON) {
  const int k = h / 2;
  for (int i = 0; i <= k; i++)
    d.vertical_line(x + k - i, y + k - i, 2 * i + 1, c);
}

void check_mark(Display &d, int x, int y, Color c = display::COLOR_ON) {  // 9 x 8 from x, y; two pixels thick
  for (int t = 0; t < 2; t++) {
    d.line(x, y + 3 + t, x + 3, y + 6 + t, c);
    d.line(x + 3, y + 6 + t, x + 8, y + 1 + t, c);
  }
}

void play_mark(Display &d, int x, int y) { d.filled_triangle(x, y, x, y + 8, x + 6, y + 4); }  // 7 x 9

// The left pane's icons, about 9 rows tall from y (the medium font's capitals are 10). Returns the width.
int row_icon(Display &d, RowIcon icon, int x, int y, Color c) {
  switch (icon) {
    case RowIcon::CLOCK:  // a dial with two hands
      d.circle(x + 3, y + 5, 3, c);
      d.vertical_line(x + 3, y + 3, 3, c);
      d.horizontal_line(x + 3, y + 5, 2, c);
      return 7;
    case RowIcon::SPEAKER:  // the body, the cone, one sound wave
      d.filled_rectangle(x, y + 4, 2, 3, c);
      d.vertical_line(x + 2, y + 3, 5, c);
      d.vertical_line(x + 3, y + 2, 7, c);
      d.vertical_line(x + 5, y + 3, 5, c);
      return 6;
    case RowIcon::LOCK:  // a padlock
      d.rectangle(x + 1, y + 1, 5, 4, c);
      d.draw_pixel_at(x + 1, y + 1, c == display::COLOR_ON ? display::COLOR_OFF : display::COLOR_ON);
      d.draw_pixel_at(x + 5, y + 1, c == display::COLOR_ON ? display::COLOR_OFF : display::COLOR_ON);
      d.filled_rectangle(x, y + 4, 7, 5, c);
      d.draw_pixel_at(x + 3, y + 6, c == display::COLOR_ON ? display::COLOR_OFF : display::COLOR_ON);
      return 7;
    case RowIcon::EXIT:  // an arrow back
      arrow_left(d, x, y + 1, 7, c);
      return 4;
    case RowIcon::SUN: {  // a 3 x 3 sun with eight one-pixel rays
      d.filled_rectangle(x + 2, y + 3, 3, 3, c);
      static const int8_t rays[8][2] = {{3, 1}, {3, 7}, {0, 4}, {6, 4}, {1, 2}, {5, 2}, {1, 6}, {5, 6}};
      for (const auto &p : rays)
        d.draw_pixel_at(x + p[0], y + p[1], c);
      return 7;
    }
    case RowIcon::MOON: {  // a crescent opening to the right
      static const int8_t px[16][2] = {{2, 0}, {3, 0}, {1, 1}, {2, 1}, {0, 2}, {1, 2}, {0, 3}, {1, 3},
                                       {0, 4}, {1, 4}, {0, 5}, {1, 5}, {1, 6}, {2, 6}, {2, 7}, {3, 7}};
      for (const auto &p : px)
        d.draw_pixel_at(x + p[0], y + 1 + p[1], c);
      return 5;
    }
    default:
      return 0;
  }
}

// Where the item sits in its row: dots, or a slider for long rows (the hours of a day).
void dots(Display &d, int n, int item, int y, int cx, int track) {
  if (n > 1 && n <= 16) {
    const int gap = std::min(6, (track - 4) / (n - 1)), x0 = cx - (n - 1) * gap / 2;
    for (int i = 0; i < n; i++) {
      if (i == item)
        d.filled_rectangle(x0 + i * gap - 1, y - 1, 3, 3);
      else
        d.draw_pixel_at(x0 + i * gap, y);
    }
  } else if (n > 16) {
    const int x0 = cx - track / 2;
    d.horizontal_line(x0, y, track);
    d.filled_rectangle(x0 + item * (track - 5) / (n - 1), y - 1, 5, 3);
  }
}

}  // namespace

// The left pane: five rows round the one on screen, which always sits in the middle, highlighted; each by its short
// name in the medium font after its icon. A dotted line marks where the list starts again (between Exit and Radio).
static void draw_rows(Display &d, Menu &menu, const MenuFonts &f) {
  const int n = menu.num_rows();
  for (int k = 0; k < VISIBLE; k++) {
    const int off = k - MIDDLE;
    if (2 * std::abs(off) >= n + (off > 0 ? 1 : 0))
      continue;  // fewer rows than lines: each row once
    const int r = menu.ring_row(off), y = k * ROW_H - 1;
    const bool sel = off == 0;
    const Color c = sel ? display::COLOR_OFF : display::COLOR_ON;
    const MenuRow &row = menu.row_at(r);
    if (sel)
      rounded_fill(d, 0, y, LW, ROW_H + 1, 2);  // one row deeper than the pitch: the tail of a j or g
    int x = 3;
    if (row.icon != RowIcon::NONE)
      x = 1 + row_icon(d, row.icon, 1, y + 2, c) + 2;
    d.print(x, y + 11, f.medium, c, TextAlign::BASELINE_LEFT, row.short_title ? row.short_title : row.title);
    if (r == 0 && k > 0 && k - 1 != MIDDLE && !sel) {
      for (int px = 1; px < LW - 1; px += 2)
        d.draw_pixel_at(px, y);
    }
  }
  d.vertical_line(LW + 1, 0, 64);
}

// CHOICES: every item side by side in the medium font, the one under the cursor white with black text, a check mark
// after the one in use. False when they do not fit.
static bool draw_choices(Display &d, const Frame &fr, display::BaseFont *font, int cx, int mid, int max_w) {
  const int gap = 8, pad = 3;
  int total = gap * (static_cast<int>(fr.all.size()) - 1);
  for (size_t i = 0; i < fr.all.size(); i++)
    total += width_of(d, font, fr.all[i]) + (static_cast<int>(i) == fr.active ? 11 : 0);
  if (total + 2 * pad > max_w)
    return false;
  int x = cx - total / 2;
  for (size_t i = 0; i < fr.all.size(); i++) {
    const int tw = width_of(d, font, fr.all[i]);
    const int w = tw + (static_cast<int>(i) == fr.active ? 11 : 0);
    const bool cur = static_cast<int>(i) == fr.item;
    const Color c = cur ? display::COLOR_OFF : display::COLOR_ON;
    if (cur)
      rounded_fill(d, x - pad, mid - 8, w + 2 * pad, 17, 2);
    d.print(x, mid + 5, font, c, TextAlign::BASELINE_LEFT, fr.all[i].c_str());
    if (static_cast<int>(i) == fr.active)
      check_mark(d, x + tw + 2, mid - 4, c);
    x += w + gap;
  }
  return true;
}

void draw_menu(Display &d, Menu &menu, const MenuFonts &f) {
  if (!menu.is_open())
    return;
  const Frame fr = frame_of(menu);
  frame_drawn(fr);
  draw_rows(d, menu, f);

  // The right pane's header: the row's full title, small, in capitals.
  std::string title = fr.title;
  for (auto &ch : title)
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  d.print(PC, 6, f.small, TextAlign::BASELINE_CENTER, title.c_str());

  if (fr.item < 0) {  // nothing to show yet: say why ("Loading list...")
    const auto lines = wrap_words(d, f.medium, fr.cap.empty() ? std::string("Empty") : fr.cap, PW - 4, 3, ELLIPSIS);
    for (size_t i = 0; i < lines.size(); i++)
      d.print(PC, 30 + static_cast<int>(i) * 15, f.medium, TextAlign::BASELINE_CENTER, lines[i].c_str());
    return;
  }

  // Exit: the clock it returns to (label: the time, caption: the next prayer) and which keys go back.
  if (fr.exit) {
    display::BaseFont *tf = width_of(d, f.large, fr.text) <= PW - 2 ? f.large : f.medium;
    d.print(PC, 29, tf, TextAlign::BASELINE_CENTER, fit_text(d, tf, fr.text, PW - 2, ELLIPSIS).c_str());
    if (!fr.cap.empty())
      d.print(PC, 41, f.small, TextAlign::BASELINE_CENTER, fit_text(d, f.small, fr.cap, PW - 2, ELLIPSIS).c_str());
    rounded_frame(d, PX + 2, 48, PW - 4, 14);
    const int hw = width_of(d, f.small, "or Select"), x0 = PC - (hw + 18) / 2;
    arrow_left(d, x0, 52, 5);
    arrow_right(d, x0 + 7, 52, 5);
    d.print(x0 + 16, 58, f.small, TextAlign::BASELINE_LEFT, "or Select");
    return;
  }

  // The caption, in the small font on at most two lines at the bottom. A tab splits it into two lines (Info).
  const bool tabbed = fr.cap.find('\t') != std::string::npos;
  std::vector<std::string> cap;
  if (!fr.cap.empty() && !tabbed)
    cap = wrap_words(d, f.small, fr.cap, PW - 2, 2, ELLIPSIS);
  const int cap_y = cap.size() == 2 ? 53 : 61;
  auto draw_caption = [&]() {
    for (size_t i = 0; i < cap.size(); i++)
      d.print(PC, cap_y + static_cast<int>(i) * 9, f.small, TextAlign::BASELINE_CENTER, cap[i].c_str());
  };

  if (!fr.all.empty() && draw_choices(d, fr, f.medium, PC, 27, PW - 2)) {
    draw_caption();
    return;
  }

  // TOGGLES: the item under the cursor ("Isha" / "Off"), and under it every item by its initial with a box, filled
  // when on; the cursor's column framed.
  if (!fr.shorts.empty()) {
    const size_t colon = fr.text.find(':');
    const std::string name = colon == std::string::npos ? fr.text : fr.text.substr(0, colon);
    d.print(PC, 20, f.medium, TextAlign::BASELINE_CENTER, name.c_str());
    d.print(PC, 34, f.medium, TextAlign::BASELINE_CENTER, fr.ons[fr.item] ? "On" : "Off");
    const int n = static_cast<int>(fr.shorts.size()), col = PW / n;
    for (int i = 0; i < n; i++) {
      const std::string s = fr.shorts[i].substr(0, 1);
      const int cx = PX + col * i + col / 2, tw = std::max(5, width_of(d, f.medium, s));
      d.print(cx, 50, f.medium, TextAlign::BASELINE_CENTER, s.c_str());
      if (fr.ons[i])
        d.filled_rectangle(cx - 2, 53, 5, 5);
      else
        d.rectangle(cx - 2, 53, 5, 5);
      if (i == fr.item)
        rounded_frame(d, cx - tw / 2 - 3, 38, tw + 7, 23);
    }
    return;
  }

  // A single item: the IP address and its caption's two parts (Info), or a button for Select (Update, Lock).
  if (fr.n == 1) {
    if (tabbed) {
      const size_t tab = fr.cap.find('\t');
      display::BaseFont *tf = f.small;
      if (width_of(d, f.medium, fr.text) <= PW - 2)
        tf = f.medium;
      else if (f.numbers != nullptr && only_digits_and_dots(fr.text) && width_of(d, f.numbers, fr.text) <= PW - 2)
        tf = f.numbers;  // an IP address
      d.print(PC, 22, tf, TextAlign::BASELINE_CENTER, fit_text(d, tf, fr.text, PW - 2, ELLIPSIS).c_str());
      for (int part = 0; part < 2; part++) {
        const std::string s = part == 0 ? fr.cap.substr(0, tab) : fr.cap.substr(tab + 1);
        display::BaseFont *pf = width_of(d, f.medium, s) <= PW ? f.medium : f.small;  // athan.local: 70 of 71
        d.print(PC, part == 0 ? 41 : 58, pf, TextAlign::BASELINE_CENTER, fit_text(d, pf, s, PW, ELLIPSIS).c_str());
      }
      return;
    }
    // The medium font while it fits the widest button ("Check now", "Installing..."), else the small one ("Install
    // 3.0.1"), cut short only past that.
    display::BaseFont *bf = width_of(d, f.medium, fr.text) <= PW - 2 ? f.medium : f.small;
    const std::string label = fit_text(d, bf, fr.text, PW - 2, ELLIPSIS);
    const int bw = std::min(PW - 2, width_of(d, bf, label) + 12);
    rounded_fill(d, PC - bw / 2, 16, bw, 19, 2);
    d.print(PC, bf == f.medium ? 30 : 28, bf, display::COLOR_OFF, TextAlign::BASELINE_CENTER, label.c_str());
    draw_caption();
    return;
  }

  // The item as large as it fits between the arrows (item_lines(): the medium font on two lines when one is too
  // narrow, "12. Abdul" / "Basit", "8" / "Al-Dosari"; then the small font on one line or two, cut short only past
  // that), arrows where Left and Right lead, the marks under it, then dots, the level bar, or the caption.
  display::BaseFont *const fonts[3] = {f.large, f.medium, f.small};
  static const int CAPS[3] = {CAP_L, CAP_M, CAP_S};
  const ItemLines item = item_lines(d, fonts, fr.text, ITEM_W, ELLIPSIS);
  const bool two = item.count == 2, tall = two && item.font == 1;  // two lines of the medium font reach lower
  const int lh = item.font == 2 ? 9 : 14;
  const int vmid = tall ? 17 : 23, amid = tall ? 26 : vmid;
  if (fr.has_left)
    arrow_left(d, PX + 1, amid - 3, 7);
  if (fr.has_right)
    arrow_right(d, W - 5, amid - 3, 7);
  const int y0 = vmid + CAPS[item.font] / 2 - (two && !tall ? lh / 2 : 0);  // two small lines: around the middle
  for (int i = 0; i < item.count; i++)
    d.print(PC, y0 + i * lh, fonts[item.font], TextAlign::BASELINE_CENTER, item.line[i].c_str());

  const int my = tall ? 40 : 34;
  const int mw = (fr.in_use ? 9 : 0) + (fr.playing ? 7 : 0) + (fr.in_use && fr.playing ? 4 : 0);
  int x = PC - mw / 2;
  if (fr.in_use) {
    check_mark(d, x, my - 1);
    x += 13;
  }
  if (fr.playing)
    play_mark(d, x, my);

  if (fr.style == RowStyle::LEVEL && fr.n > 1) {
    const int bx = PX + 6, bw = PW - 12;  // a bar filled up to the level
    d.rectangle(bx, 45, bw, 5);
    const int fill = fr.item * (bw - 2) / (fr.n - 1);
    if (fill > 0)
      d.filled_rectangle(bx + 1, 46, fill, 3);
  } else if (cap.size() <= 1) {
    dots(d, fr.n, fr.item, tall ? 54 : 49, PC, PW - 10);
  }
  draw_caption();
}

#endif  // ATHAN_DISPLAY_TFT

}  // namespace athan
}  // namespace esphome
