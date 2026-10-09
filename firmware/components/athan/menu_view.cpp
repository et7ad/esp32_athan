#include "menu_view.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace esphome {
namespace athan {

using display::Display;
using esphome::Color;
using display::TextAlign;

namespace {

// Two panes: the rows by name on the left (a ring, the one on screen in the middle), a line, the row's items on the
// right. Right pane, top to bottom (y of text baselines): the item around y 20, its marks at 31, dots at 46, the
// caption at 60 (two lines: 52 and 61).
const int W = 128;
const int LW = 50;                           // left pane width
const int PX = LW + 2, PW = W - PX;          // right pane: x 52..127
const int PC = PX + PW / 2;                  // its centre
const int ROW_H = 9, VISIBLE = 7, MIDDLE = 3;  // left pane: 7 rows of 9 px, the one on screen 4th
const int CAP_L = 14, CAP_M = 10, CAP_S = 5;   // capital heights of the large, medium and small fonts

int width_of(Display &d, display::BaseFont *f, const std::string &s) {
  int x1, y1, w, h;
  d.get_text_bounds(0, 0, s.c_str(), f, TextAlign::TOP_LEFT, &x1, &y1, &w, &h);
  return w;
}

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

// Words of s on lines at most w pixels wide (a word wider than w gets a line of its own).
std::vector<std::string> wrap_words(Display &d, display::BaseFont *f, const std::string &s, int w) {
  std::vector<std::string> out;
  std::string line;
  size_t i = 0;
  while (i <= s.size()) {
    size_t j = s.find(' ', i);
    if (j == std::string::npos)
      j = s.size();
    const std::string word = s.substr(i, j - i);
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
  return out;
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

// Everything the menu screen shows, read from the hooks once per draw. Its fingerprint tells whether a redraw would
// change anything (menu_changed()).
struct Frame {
  int row{0};
  const char *title{""};
  RowStyle style{RowStyle::LIST};
  bool exit{false};
  std::string text, cap;
  int item{-1}, n{0}, active{-1};
  bool in_use{false}, playing{false}, has_left{false}, has_right{false};
  std::vector<std::string> all;     // CHOICES: every item, side by side when they fit
  std::vector<std::string> shorts;  // TOGGLES: every item's short name
  std::vector<bool> ons;            // TOGGLES: on or off
};

const int MAX_SIDE_BY_SIDE = 6;  // at most this many items in a CHOICES or TOGGLES line

static Frame frame_of(Menu &menu) {
  menu.refresh();
  const MenuRow &row = menu.row_at(menu.row());
  Frame fr;
  fr.row = menu.row();
  fr.title = row.title;
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
  const int32_t nums[8] = {fr.row, fr.item, fr.n, fr.in_use, fr.playing, fr.active, fr.has_left, fr.has_right};
  mix(reinterpret_cast<const char *>(nums), sizeof(nums));
  return h;
}

static uint32_t drawn_fingerprint = 0;  // of the last menu screen sent to the display

bool menu_changed(Menu &menu) { return menu.is_open() && fingerprint(frame_of(menu)) != drawn_fingerprint; }

// The left pane: the rows round the one on screen, which always sits in the middle, highlighted. A dotted line marks
// where the list starts again (between Exit and Radio).
static void draw_rows(Display &d, Menu &menu, const MenuFonts &f) {
  const int n = menu.num_rows();
  for (int k = 0; k < VISIBLE; k++) {
    const int off = k - MIDDLE;
    if (2 * std::abs(off) >= n + (off > 0 ? 1 : 0))
      continue;  // fewer rows than lines: each row once
    const int r = menu.ring_row(off), y = k * ROW_H + 1;
    const bool sel = off == 0;
    const Color c = sel ? display::COLOR_OFF : display::COLOR_ON;
    const MenuRow &row = menu.row_at(r);
    if (sel)
      rounded_fill(d, 0, y, LW, ROW_H, 1);
    if (row.exit) {
      arrow_left(d, 3, y + 2, 5, c);
      d.print(9, y + 7, f.small, c, TextAlign::BASELINE_LEFT, row.title);
    } else {
      d.print(3, y + 7, f.small, c, TextAlign::BASELINE_LEFT, row.title);
    }
    if (r == 0 && k > 0 && k - 1 != MIDDLE && !sel) {
      for (int x = 1; x < LW - 1; x += 2)
        d.draw_pixel_at(x, y);
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
  drawn_fingerprint = fingerprint(fr);
  draw_rows(d, menu, f);

  if (fr.item < 0) {  // nothing to show yet: say why ("Loading list...")
    const auto lines = wrap_words(d, f.medium, fr.cap.empty() ? std::string("Empty") : fr.cap, PW - 4);
    for (size_t i = 0; i < lines.size() && i < 3; i++)
      d.print(PC, 30 + static_cast<int>(i) * 15, f.medium, TextAlign::BASELINE_CENTER, lines[i].c_str());
    return;
  }

  // Exit: the clock it returns to (label: the time, caption: the next prayer) and which keys go back.
  if (fr.exit) {
    display::BaseFont *tf = width_of(d, f.large, fr.text) <= PW - 2 ? f.large : f.medium;
    d.print(PC, 26, tf, TextAlign::BASELINE_CENTER, fr.text.c_str());
    if (!fr.cap.empty())
      d.print(PC, 40, f.small, TextAlign::BASELINE_CENTER, fr.cap.c_str());
    rounded_frame(d, PX + 3, 48, PW - 6, 14);
    const int hw = width_of(d, f.small, "or Select"), x0 = PC - (hw + 18) / 2;
    arrow_left(d, x0, 52, 5);
    arrow_right(d, x0 + 7, 52, 5);
    d.print(x0 + 16, 58, f.small, TextAlign::BASELINE_LEFT, "or Select");
    return;
  }

  // The caption, in the small font on at most two lines at the bottom. A tab splits it into two lines (Info).
  const bool tabbed = fr.cap.find('\t') != std::string::npos;
  std::vector<std::string> cap;
  if (!fr.cap.empty() && !tabbed) {
    cap = wrap_words(d, f.small, fr.cap, PW - 2);
    if (cap.size() > 2)
      cap.resize(2);
  }
  const int cap_y = cap.size() == 2 ? 52 : 60;
  auto draw_caption = [&]() {
    for (size_t i = 0; i < cap.size(); i++)
      d.print(PC, cap_y + static_cast<int>(i) * 9, f.small, TextAlign::BASELINE_CENTER, cap[i].c_str());
  };

  if (!fr.all.empty() && draw_choices(d, fr, f.medium, PC, 24, PW - 2)) {
    draw_caption();
    return;
  }

  // TOGGLES: the item under the cursor ("Isha" / "Off"), and under it every item by its initial with a box, filled
  // when on; the cursor's column framed.
  if (!fr.shorts.empty()) {
    const size_t colon = fr.text.find(':');
    const std::string name = colon == std::string::npos ? fr.text : fr.text.substr(0, colon);
    d.print(PC, 19, f.medium, TextAlign::BASELINE_CENTER, name.c_str());
    d.print(PC, 34, f.medium, TextAlign::BASELINE_CENTER, fr.ons[fr.item] ? "On" : "Off");
    const int n = static_cast<int>(fr.shorts.size()), col = PW / n, top = 44;
    for (int i = 0; i < n; i++) {
      const std::string s = fr.shorts[i].substr(0, 1);
      const int cx = PX + col * i + col / 2, tw = std::max(5, width_of(d, f.small, s));
      d.print(cx, top + 5, f.small, TextAlign::BASELINE_CENTER, s.c_str());
      if (fr.ons[i])
        d.filled_rectangle(cx - 2, top + 8, 5, 5);
      else
        d.rectangle(cx - 2, top + 8, 5, 5);
      if (i == fr.item)
        rounded_frame(d, cx - tw / 2 - 3, top - 2, tw + 7, 16);
    }
    return;
  }

  // A single item: the IP address and its caption's two parts (Info), or a button for Select (Update, Lock).
  if (fr.n == 1) {
    if (tabbed) {
      const size_t tab = fr.cap.find('\t');
      display::BaseFont *tf = width_of(d, f.medium, fr.text) <= PW - 2 ? f.medium : f.small;
      d.print(PC, 24, tf, TextAlign::BASELINE_CENTER, fr.text.c_str());
      d.print(PC, 42, f.small, TextAlign::BASELINE_CENTER, fr.cap.substr(0, tab).c_str());
      d.print(PC, 54, f.small, TextAlign::BASELINE_CENTER, fr.cap.substr(tab + 1).c_str());
      return;
    }
    const int bw = std::min(PW - 2, width_of(d, f.medium, fr.text) + 12);
    rounded_fill(d, PC - bw / 2, 15, bw, 19, 2);
    d.print(PC, 29, f.medium, display::COLOR_OFF, TextAlign::BASELINE_CENTER, fr.text.c_str());
    draw_caption();
    return;
  }

  // The item as large as it fits (the medium font on two lines when one is too narrow: "8" / "Al-Dosari"), arrows
  // where Left and Right lead, the marks under it, then dots, the level bar, or the caption.
  const int max_w = PW - 14;
  std::string lines[2] = {fr.text, ""};
  int nl = 1, cap_h = CAP_L;
  display::BaseFont *font = f.large;
  if (width_of(d, f.large, fr.text) > max_w) {
    font = f.medium;
    cap_h = CAP_M;
    const size_t sp = fr.text.find(' ');
    if (width_of(d, f.medium, fr.text) > max_w) {
      if (sp != std::string::npos && width_of(d, f.medium, fr.text.substr(sp + 1)) <= max_w) {
        lines[0] = fr.text.substr(0, sp);
        lines[1] = fr.text.substr(sp + 1);
        nl = 2;
      } else {
        font = f.small;
        cap_h = CAP_S;
      }
    }
  }
  const bool two = nl == 2;
  const int vmid = two ? 15 : 20, amid = two ? 22 : vmid;
  if (fr.has_left)
    arrow_left(d, PX + 1, amid - 3, 7);
  if (fr.has_right)
    arrow_right(d, W - 5, amid - 3, 7);
  for (int i = 0; i < nl; i++)
    d.print(PC, vmid + cap_h / 2 + i * 14, font, TextAlign::BASELINE_CENTER, lines[i].c_str());

  const int my = two ? 38 : 31;
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
    d.rectangle(bx, 42, bw, 5);
    const int fill = fr.item * (bw - 2) / (fr.n - 1);
    if (fill > 0)
      d.filled_rectangle(bx + 1, 43, fill, 3);
  } else if (cap.size() <= 1) {
    dots(d, fr.n, fr.item, two ? 53 : 46, PC, PW - 10);
  }
  draw_caption();
}

}  // namespace athan
}  // namespace esphome
