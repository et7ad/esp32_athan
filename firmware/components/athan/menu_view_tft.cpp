// The menu on the 240x240 colour TFT (ATHAN_DISPLAY_TFT): the OLED's two panes (menu_view.cpp) grown, in colour.
#include "menu_view.h"

#ifdef ATHAN_DISPLAY_TFT

#include "menu_frame.h"
#include "tft_draw.h"
#include "tft_theme.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace esphome {
namespace athan {

using display::Display;
using display::TextAlign;
using esphome::Color;

namespace {

// Left: five rows of 48 px, a ring, the row on screen in the middle on a bar in the accent; each row by its icon and
// its name (the short name when the title is wider than 60 px). Right (x 112-237): the title in capitals at
// baseline 22, then the item centred around y 116, its marks under it, and at the bottom dots, a level bar or the
// caption.
const int W = 240;
const int LW = 104;
const int ROW_H = 48, VISIBLE = 5, MIDDLE = 2;
const int PX = LW + 8, PW = W - PX - 2, PC = PX + PW / 2;
const int TOP = 30, BOTTOM = 236;
const int MID = TOP + (BOTTOM - TOP) * 42 / 100;  // the item's centre line

struct Theme {
  Color acc, fg, sub, dim, line, ink, bg;
};

int width(Display &d, display::BaseFont *f, const std::string &s) { return tft::text_width(d, f, s.c_str()); }

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
    if (line.empty() || width(d, f, t) <= w) {
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

// Centres of n round items around line y: one line when 34 px each fits across the pane, else two (3 + 2).
std::vector<std::pair<int, int>> round_spots(int n, int y) {
  std::vector<std::pair<int, int>> pts;
  const int one = (PW - 8) / std::max(1, n);
  if (one >= 34) {
    const int gap = std::min(42, one);
    for (int i = 0; i < n; i++)
      pts.emplace_back(PC - gap * (n - 1) / 2 + i * gap, y);
    return pts;
  }
  const int per = (n + 1) / 2, gap = std::min(40, (PW - 8) / per);
  for (int i = 0; i < n; i++) {
    const int line = i < per ? 0 : 1, k = line ? i - per : i, cnt = line ? n - per : per;
    pts.emplace_back(PC - gap * (cnt - 1) / 2 + k * gap, y - 4 + line * 36);
  }
  return pts;
}

// The caption: up to two lines, the last on baseline 228.
void caption(Display &d, const MenuFonts &f, const Theme &th, const std::string &cap) {
  auto lines = wrap_words(d, f.small, cap, PW - 8);
  if (lines.size() > 2)
    lines.resize(2);
  const int y0 = BOTTOM - 8 - (lines.size() > 1 ? 17 : 0);
  for (size_t i = 0; i < lines.size(); i++)
    d.print(PC, y0 + static_cast<int>(i) * 17, f.small, th.sub, TextAlign::BASELINE_CENTER, lines[i].c_str());
}

bool only_digits_and_dots(const std::string &s) {
  return !s.empty() && s.find_first_not_of("0123456789.") == std::string::npos;
}

void draw_rows(Display &d, Menu &menu, const MenuFonts &f, const Theme &th) {
  const int n = menu.num_rows();
  for (int k = 0; k < VISIBLE; k++) {
    const int off = k - MIDDLE;
    if (2 * std::abs(off) >= n + (off > 0 ? 1 : 0))
      continue;  // fewer rows than lines: each row once
    const int r = menu.ring_row(off), y = k * ROW_H;
    const bool sel = off == 0, near = std::abs(off) == 1;
    const MenuRow &row = menu.row_at(r);
    Color bg = th.bg;
    if (sel) {
      tft::fill_round_rect(d, 4, y + 4, LW - 4, 40, 12, th.acc, th.bg);
      bg = th.acc;
    }
    if (row.symbol != nullptr)
      tft::icon(d, 12, y + 13, f.icons, 22, row.symbol, sel ? th.ink : near ? th.acc : th.dim, bg);
    const bool fits = width(d, f.row, row.title) <= LW - 44;
    const char *name = fits || row.short_title == nullptr ? row.title : row.short_title;
    d.print(40, y + 30, f.row, sel ? th.ink : near ? th.fg : th.dim, TextAlign::BASELINE_LEFT, name, bg);
    if (r == 0 && k > 0 && k - 1 != MIDDLE && !sel)
      d.horizontal_line(10, y, LW - 14, th.line);  // where the list starts again
  }
  d.vertical_line(LW + 3, 10, 220, th.line);
}

// CHOICES side by side as chips, the one under the cursor filled with the accent, a check after the one in use.
// False when they do not fit across the pane.
bool draw_chips(Display &d, const Frame &fr, const MenuFonts &f, const Theme &th) {
  const int pad = 12, gap = 8;
  std::vector<int> ws;
  int total = gap * (static_cast<int>(fr.all.size()) - 1);
  for (size_t i = 0; i < fr.all.size(); i++) {
    ws.push_back(width(d, f.row, fr.all[i]) + 2 * pad + (static_cast<int>(i) == fr.active ? 18 : 0));
    total += ws.back();
  }
  if (total > PW - 4)
    return false;
  int x = PC - total / 2;
  const int y = MID - 16;
  for (size_t i = 0; i < fr.all.size(); i++) {
    const bool cur = static_cast<int>(i) == fr.item;
    const Color bg = cur ? th.acc : th.bg;
    if (cur)
      tft::fill_round_rect(d, x, y, ws[i], 32, 16, th.acc, th.bg);
    else
      tft::round_rect_frame(d, x, y, ws[i], 32, 16, 2, th.line, th.bg);
    d.print(x + pad, y + 22, f.row, cur ? th.ink : th.fg, TextAlign::BASELINE_LEFT, fr.all[i].c_str(), bg);
    if (static_cast<int>(i) == fr.active)
      tft::icon(d, x + pad + width(d, f.row, fr.all[i]) + 2, y + 8, f.icons_small, 16, sym::CHECK,
                cur ? th.ink : th.acc, bg);
    x += ws[i] + gap;
  }
  return true;
}

void draw_item(Display &d, const Frame &fr, const MenuFonts &f, const Theme &th) {
  // Exit: the clock it returns to (label: the time, caption: the next prayer) and which keys go back.
  if (fr.exit) {
    d.print(PC, 104, f.time, th.fg, TextAlign::BASELINE_CENTER, fr.text.c_str());
    if (!fr.cap.empty())
      d.print(PC, 130, f.item_small, th.acc, TextAlign::BASELINE_CENTER, fr.cap.c_str());
    const int y = 218, hw = width(d, f.small, "or Select") + 40, x0 = PC - hw / 2;
    tft::round_rect_frame(d, PX + 6, y - 14, PW - 12, 30, 15, 2, th.line, th.bg);
    tft::icon(d, x0, y - 11, f.icons, 22, sym::CHEVRON_LEFT, th.sub);
    tft::icon(d, x0 + 16, y - 11, f.icons, 22, sym::CHEVRON_RIGHT, th.sub);
    d.print(x0 + 40, y + 5, f.small, th.sub, TextAlign::BASELINE_LEFT, "or Select");
    return;
  }
  if (fr.item < 0) {  // nothing to show yet: say why ("Loading list...")
    const auto lines = wrap_words(d, f.row, fr.cap.empty() ? std::string("Empty") : fr.cap, PW - 8);
    for (size_t i = 0; i < lines.size() && i < 4; i++)
      d.print(PC, MID + static_cast<int>(i) * 22, f.row, th.sub, TextAlign::BASELINE_CENTER, lines[i].c_str());
    return;
  }
  const bool tabbed = fr.cap.find('\t') != std::string::npos;

  // SWATCHES: the colour's name, and a dot in each colour (the cursor's ringed, a check on the one in use).
  if (!fr.swatches.empty()) {
    d.print(PC, MID - 18, f.item_small, th.fg, TextAlign::BASELINE_CENTER, fr.text.c_str());
    const auto pts = round_spots(static_cast<int>(fr.swatches.size()), MID + 18);
    for (size_t i = 0; i < pts.size(); i++) {
      const Color c(fr.swatches[i]);
      tft::fill_circle(d, pts[i].first, pts[i].second, 12, c, th.bg);
      if (static_cast<int>(i) == fr.item)
        tft::circle_frame(d, pts[i].first, pts[i].second, 16, 2, th.fg, th.bg);
      if (static_cast<int>(i) == fr.active)
        tft::icon(d, pts[i].first - 8, pts[i].second - 8, f.icons_small, 16, sym::CHECK, th.ink, c);
    }
    return;
  }

  // TOGGLES: the prayer under the cursor and its state, then every prayer as a round switch with its initial: filled
  // with the accent when on, a grey ring when off; the cursor's ringed in white.
  if (!fr.shorts.empty()) {
    const size_t colon = fr.text.find(':');
    const std::string name = (colon == std::string::npos ? fr.text : fr.text.substr(0, colon)) + "  ";
    const bool on = fr.ons[fr.item];
    const char *state = on ? "On" : "Off";
    const int x0 = PC - (width(d, f.item_small, name) + width(d, f.item_small, state)) / 2;
    d.print(x0, MID - 12, f.item_small, th.fg, TextAlign::BASELINE_LEFT, name.c_str());
    d.print(x0 + width(d, f.item_small, name), MID - 12, f.item_small, on ? th.acc : th.sub, TextAlign::BASELINE_LEFT,
            state);
    const auto pts = round_spots(static_cast<int>(fr.shorts.size()), MID + 22);
    for (size_t i = 0; i < pts.size(); i++) {
      const int px = pts[i].first, py = pts[i].second;
      if (fr.ons[i])
        tft::fill_circle(d, px, py, 14, th.acc, th.bg);
      else
        tft::circle_frame(d, px, py, 13, 2, th.dim, th.bg);
      if (static_cast<int>(i) == fr.item)
        tft::circle_frame(d, px, py, 18, 2, th.fg, th.bg);
      const std::string ch = fr.shorts[i].substr(0, 1);
      d.print(px, py + 6, f.row, fr.ons[i] ? th.ink : th.sub, TextAlign::BASELINE_CENTER, ch.c_str(),
              fr.ons[i] ? th.acc : th.bg);
    }
    if (pts.back().second < MID + 40)
      caption(d, f, th, "Select: on / off");
    return;
  }

  if (!fr.all.empty() && draw_chips(d, fr, f, th)) {
    if (!fr.cap.empty())
      caption(d, f, th, fr.cap);
    return;
  }

  // A single item: the IP address with the web page's name and the version (Info), or a button for Select.
  if (fr.n == 1) {
    if (tabbed) {
      const bool ip = only_digits_and_dots(fr.text);
      tft::icon(d, PC - 11, TOP + 6, f.icons, 22, ip ? sym::WIFI : sym::WIFI_OFF, ip ? th.acc : Color(tft::BAD));
      display::BaseFont *tf = width(d, f.item_small, fr.text) <= PW - 4 ? f.item_small : f.small;
      d.print(PC, 133, tf, th.fg, TextAlign::BASELINE_CENTER, fr.text.c_str());
      const size_t tab = fr.cap.find('\t');
      d.print(PC, 157, f.small, th.sub, TextAlign::BASELINE_CENTER, fr.cap.substr(0, tab).c_str());
      d.print(PC, 177, f.small, th.sub, TextAlign::BASELINE_CENTER, fr.cap.substr(tab + 1).c_str());
      return;
    }
    const int bw = std::min(PW - 8, width(d, f.row, fr.text) + 32);
    tft::fill_round_rect(d, PC - bw / 2, MID - 18, bw, 36, 18, th.acc, th.bg);
    d.print(PC, MID + 6, f.row, th.ink, TextAlign::BASELINE_CENTER, fr.text.c_str(), th.acc);
    if (!fr.cap.empty())
      caption(d, f, th, fr.cap);
    return;
  }

  // A list or a level: the item as large as it fits (18 px on two lines when one is too narrow: "8" / "Al-Dosari"),
  // arrows where Left and Right lead, the marks under it, then dots, the level bar or the caption.
  const int max_w = PW - 2 * 24 - 4;
  std::string lines[2] = {fr.text, ""};
  int nl = 1, size = 22;
  display::BaseFont *font = f.item;
  if (width(d, f.item, fr.text) > max_w) {
    font = f.item_small;
    size = 18;
    if (width(d, f.item_small, fr.text) > max_w) {
      const size_t sp = fr.text.find(' ');
      if (sp != std::string::npos && width(d, f.item_small, fr.text.substr(0, sp)) <= max_w &&
          width(d, f.item_small, fr.text.substr(sp + 1)) <= max_w) {
        lines[0] = fr.text.substr(0, sp);
        lines[1] = fr.text.substr(sp + 1);
        nl = 2;
      } else {
        font = f.small;
        size = 13;
      }
    }
  }
  const int lh = size + 4, ty = MID + size * 36 / 100 - (nl - 1) * lh / 2;
  for (int i = 0; i < nl; i++)
    d.print(PC, ty + i * lh, font, th.fg, TextAlign::BASELINE_CENTER, lines[i].c_str());
  if (fr.has_left)
    tft::icon(d, PX, MID - 11, f.icons, 22, sym::CHEVRON_LEFT, th.sub);
  if (fr.has_right)
    tft::icon(d, PX + PW - 22, MID - 11, f.icons, 22, sym::CHEVRON_RIGHT, th.sub);

  const char *marks[2];
  int nm = 0;
  if (fr.in_use && fr.kind != RowKind::VALUE)  // a value row is always at its value: no check
    marks[nm++] = sym::CHECK_CIRCLE;
  if (fr.playing)
    marks[nm++] = sym::GRAPHIC_EQ;
  const int my = ty + (nl - 1) * lh + 8;
  for (int i = 0; i < nm; i++)
    tft::icon(d, PC - nm * 11 + i * 22, my, f.icons, 22, marks[i], th.acc);

  if (fr.style == RowStyle::LEVEL && fr.n > 1) {
    const int bx = PX + 14, bw = PW - 28, y = my + 6;
    tft::fill_round_rect(d, bx, y, bw, 10, 5, th.line, th.bg);
    if (fr.item > 0)  // 0 % (Brightness): an empty bar
      tft::fill_round_rect(d, bx, y, std::max(10, bw * fr.item / (fr.n - 1)), 10, 5, th.acc, th.line);
    if (!fr.cap.empty())
      caption(d, f, th, fr.cap);
  } else if (!fr.cap.empty()) {
    caption(d, f, th, fr.cap);
  } else if (fr.n > 1 && fr.n <= 12) {
    const int y = BOTTOM - 12, gap = std::min(13, (PW - 28) / (fr.n - 1)), x0 = PC - (fr.n - 1) * gap / 2;
    for (int i = 0; i < fr.n; i++) {
      if (i == fr.item)
        tft::fill_circle(d, x0 + i * gap, y, 3.5f, th.acc, th.bg);
      else
        tft::fill_circle(d, x0 + i * gap, y, 2, th.dim, th.bg);
    }
  } else if (fr.n > 12) {  // a slider: the hours of a day
    const int y = BOTTOM - 12, track = PW - 20, x0 = PC - track / 2;
    d.filled_rectangle(x0, y - 1, track, 3, th.line);
    tft::fill_circle(d, x0 + fr.item * track / (fr.n - 1), y + 0.5f, 5, th.acc, th.bg);
  }
}

}  // namespace

void draw_menu(Display &d, Menu &menu, const MenuFonts &f) {
  if (!menu.is_open())
    return;
  const Frame fr = frame_of(menu);
  frame_drawn(fr);
  const Theme th{Color(f.accent), Color(tft::FG), Color(tft::SUB), Color(tft::DIM),
                 Color(tft::LINE), Color(tft::INK), Color(0, 0, 0)};
  draw_rows(d, menu, f, th);
  std::string title = fr.title;
  for (auto &ch : title)
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  d.print(PC, 22, f.label, th.acc, TextAlign::BASELINE_CENTER, title.c_str());
  draw_item(d, fr, f, th);
}

}  // namespace athan
}  // namespace esphome

#endif  // ATHAN_DISPLAY_TFT
