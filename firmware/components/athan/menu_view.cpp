#include "menu_view.h"

#include <algorithm>

namespace esphome {
namespace athan {

using display::Display;
using display::TextAlign;

namespace {

// Layout, top to bottom (y of text baselines):
//   row above 8 | title 22 | item centred on y 35 (33 with a caption) | dots 50 or caption 52 | row below 62
const int W = 128;
const int SIDE = 22;  // width of the faded item previews at the left and right edges
const int Y_ABOVE = 8, Y_TITLE = 22, Y_DOTS = 50, Y_CAPTION = 52, Y_BELOW = 62;
const int ITEM_TOP = 26, ITEM_BOTTOM = 48;  // band of the item and its side previews

int width_of(Display &d, display::BaseFont *f, const std::string &s) {
  int x1, y1, w, h;
  d.get_text_bounds(0, 0, s.c_str(), f, TextAlign::TOP_LEFT, &x1, &y1, &w, &h);
  return w;
}

// Clears one pixel in four (even x on even y) in [x0, x1) x [y0, y1): text there looks dimmer on a one-colour
// OLED and stays readable at 10 px (a 50 % checkerboard made it noise).
void fade(Display &d, int x0, int y0, int x1, int y1) {
  for (int y = y0 + (y0 & 1); y < y1; y += 2)
    for (int x = x0 + (x0 & 1); x < x1; x += 2)
      d.draw_pixel_at(x, y, display::COLOR_OFF);
}

// Faded text, clipped to [x0, x1) x [y0, y1). Drawn before the sharp parts, which never reach into that box.
void ghost(Display &d, display::BaseFont *f, int x, int y, TextAlign align, const std::string &s, int x0, int y0,
           int x1, int y1) {
  d.start_clipping(x0, y0, x1, y1);
  d.print(x, y, f, align, s.c_str());
  fade(d, x0, y0, x1, y1);
  d.end_clipping();
}

// A faded line across the screen (the rows above and below): centred, or from the left edge when too long.
void ghost_line(Display &d, display::BaseFont *f, const std::string &s, int baseline, int y0, int y1) {
  if (width_of(d, f, s) <= W - 4)
    ghost(d, f, W / 2, baseline, TextAlign::BASELINE_CENTER, s, 0, y0, W, y1);
  else
    ghost(d, f, 2, baseline, TextAlign::BASELINE_LEFT, s, 0, y0, W, y1);
}

// "Volume: 60%": a row's title and its item in use. Past the first and the last row: the clock.
std::string summary(const Menu &menu, int r) {
  if (r < 0)
    return "Home";
  const MenuRow &row = menu.row_at(r);
  std::string s = row.title;
  const int n = row.count ? row.count() : 0;
  const int a = row.active ? row.active() : -1;
  if (a >= 0 && a < n && row.label)
    s += ": " + row.label(a);
  return s;
}

void check_mark(Display &d, int x, int y) {  // 9 x 8 from x, y; two pixels thick
  for (int t = 0; t < 2; t++) {
    d.line(x, y + 3 + t, x + 3, y + 6 + t);
    d.line(x + 3, y + 6 + t, x + 8, y + 1 + t);
  }
}

void play_mark(Display &d, int x, int y) { d.filled_triangle(x, y, x, y + 8, x + 6, y + 4); }  // 7 x 9

}  // namespace

void draw_menu(Display &d, Menu &menu, const MenuFonts &f) {
  if (!menu.is_open())
    return;
  menu.refresh();
  const MenuRow &row = menu.row_at(menu.row());
  const int item = menu.item();

  ghost_line(d, f.small, summary(menu, menu.row_above()), Y_ABOVE, 0, 11);
  ghost_line(d, f.small, summary(menu, menu.row_below()), Y_BELOW, 54, 64);
  d.print(W / 2, Y_TITLE, f.medium, TextAlign::BASELINE_CENTER, row.title);

  if (item < 0) {  // nothing to show yet: say why ("Loading list...")
    const std::string why = row.caption ? row.caption(-1) : std::string();
    d.print(W / 2, 40, f.medium, TextAlign::BASELINE_CENTER, why.empty() ? "Empty" : why.c_str());
    return;
  }

  const std::string text = row.label ? row.label(item) : std::string();
  const std::string cap = row.caption ? row.caption(item) : std::string();
  const bool in_use = row.active && row.active() == item;
  const bool playing = row.playing && row.playing() == item;
  const int marks = (in_use ? 9 : 0) + (playing ? 7 : 0) + (in_use && playing ? 2 : 0);

  // The biggest font that fits: large between the side previews, medium there, medium over the whole width (no
  // side previews), small over the whole width. With a caption the large font does not fit vertically.
  struct Fit {
    display::BaseFont *font;
    int cap_h;
    bool sides;
  };
  const Fit fits[] = {{f.large, 14, true}, {f.medium, 10, true}, {f.medium, 10, false}, {f.small, 7, false}};
  Fit fit = fits[3];
  int group = 0;  // item text plus its marks
  int tw = 0;
  for (const Fit &c : fits) {
    if (!cap.empty() && c.font == f.large)
      continue;
    fit = c;
    tw = width_of(d, c.font, text);
    group = tw + (marks ? marks + 3 : 0);
    if (group <= (c.sides ? W - 2 * (SIDE + 2) : W - 2))
      break;
  }
  const int mid = cap.empty() ? 35 : 33;  // vertical centre of the item's capitals

  if (fit.sides) {
    const int l = menu.item_left(), r = menu.item_right();
    const int sb = mid + 3;  // small capitals (7 px) centred on the item
    if (l >= 0)
      ghost(d, f.small, SIDE - 1, sb, TextAlign::BASELINE_RIGHT, row.label(l), 0, ITEM_TOP, SIDE, ITEM_BOTTOM);
    if (r >= 0)
      ghost(d, f.small, W - SIDE + 1, sb, TextAlign::BASELINE_LEFT, row.label(r), W - SIDE, ITEM_TOP, W, ITEM_BOTTOM);
  }

  // The item and its marks, centred together.
  int x = std::max(1, (W - group) / 2);
  d.print(x, mid + fit.cap_h / 2, fit.font, TextAlign::BASELINE_LEFT, text.c_str());
  x += tw + 3;
  if (in_use) {
    check_mark(d, x, mid - 4);
    x += 11;
  }
  if (playing)
    play_mark(d, x, mid - 4);

  // Under the item: the caption, or where the item sits in the row.
  const int n = menu.count();
  if (!cap.empty()) {
    if (width_of(d, f.small, cap) <= W - 2)
      d.print(W / 2, Y_CAPTION, f.small, TextAlign::BASELINE_CENTER, cap.c_str());
    else
      d.print(1, Y_CAPTION, f.small, TextAlign::BASELINE_LEFT, cap.c_str());
  } else if (n > 1 && n <= 16) {
    const int gap = 6, x0 = W / 2 - (n - 1) * gap / 2;
    for (int i = 0; i < n; i++) {
      if (i == item)
        d.filled_rectangle(x0 + i * gap - 1, Y_DOTS - 1, 3, 3);
      else
        d.draw_pixel_at(x0 + i * gap, Y_DOTS);
    }
  } else if (n > 16) {
    const int track = 64, x0 = W / 2 - track / 2;  // a slider: the hours of a day
    d.horizontal_line(x0, Y_DOTS, track);
    d.filled_rectangle(x0 + item * (track - 5) / (n - 1), Y_DOTS - 1, 5, 3);
  }
}

}  // namespace athan
}  // namespace esphome
