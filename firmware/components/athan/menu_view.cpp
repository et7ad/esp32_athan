#include "menu_view.h"

#include <algorithm>
#include <cstring>

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

// Everything the menu screen shows, read from the row's hooks once per draw. Its fingerprint tells whether a
// redraw would change anything (menu_changed()).
struct Frame {
  const char *title{""};
  std::string above, below, text, cap, left, right;
  int item{-1}, n{0};
  bool in_use{false}, playing{false};
};

static Frame frame_of(Menu &menu) {
  menu.refresh();
  const MenuRow &row = menu.row_at(menu.row());
  Frame fr;
  fr.title = row.title;
  fr.above = summary(menu, menu.row_above());
  fr.below = summary(menu, menu.row_below());
  fr.item = menu.item();
  fr.n = menu.count();
  if (fr.item < 0) {
    fr.cap = row.caption ? row.caption(-1) : std::string();  // why there is nothing to show
    return fr;
  }
  fr.text = row.label ? row.label(fr.item) : std::string();
  fr.cap = row.caption ? row.caption(fr.item) : std::string();
  fr.in_use = row.active && row.active() == fr.item;
  fr.playing = row.playing && row.playing() == fr.item;
  const int l = menu.item_left(), r = menu.item_right();
  if (l >= 0)
    fr.left = row.label(l);
  if (r >= 0)
    fr.right = row.label(r);
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
  str(fr.above);
  str(fr.below);
  str(fr.text);
  str(fr.cap);
  str(fr.left);
  str(fr.right);
  const int32_t nums[4] = {fr.item, fr.n, fr.in_use, fr.playing};
  mix(reinterpret_cast<const char *>(nums), sizeof(nums));
  return h;
}

static uint32_t drawn_fingerprint = 0;  // of the last menu screen sent to the display

bool menu_changed(Menu &menu) { return menu.is_open() && fingerprint(frame_of(menu)) != drawn_fingerprint; }

void draw_menu(Display &d, Menu &menu, const MenuFonts &f) {
  if (!menu.is_open())
    return;
  const Frame fr = frame_of(menu);
  drawn_fingerprint = fingerprint(fr);

  ghost_line(d, f.small, fr.above, Y_ABOVE, 0, 11);
  ghost_line(d, f.small, fr.below, Y_BELOW, 54, 64);
  d.print(W / 2, Y_TITLE, f.medium, TextAlign::BASELINE_CENTER, fr.title);

  if (fr.item < 0) {  // nothing to show yet: say why ("Loading list...")
    d.print(W / 2, 40, f.medium, TextAlign::BASELINE_CENTER, fr.cap.empty() ? "Empty" : fr.cap.c_str());
    return;
  }

  const int marks = (fr.in_use ? 9 : 0) + (fr.playing ? 7 : 0) + (fr.in_use && fr.playing ? 2 : 0);

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
    if (!fr.cap.empty() && c.font == f.large)
      continue;
    fit = c;
    tw = width_of(d, c.font, fr.text);
    group = tw + (marks ? marks + 3 : 0);
    if (group <= (c.sides ? W - 2 * (SIDE + 2) : W - 2))
      break;
  }
  const int mid = fr.cap.empty() ? 35 : 33;  // vertical centre of the item's capitals

  if (fit.sides) {
    const int sb = mid + 3;  // small capitals (7 px) centred on the item
    if (!fr.left.empty())
      ghost(d, f.small, SIDE - 1, sb, TextAlign::BASELINE_RIGHT, fr.left, 0, ITEM_TOP, SIDE, ITEM_BOTTOM);
    if (!fr.right.empty())
      ghost(d, f.small, W - SIDE + 1, sb, TextAlign::BASELINE_LEFT, fr.right, W - SIDE, ITEM_TOP, W, ITEM_BOTTOM);
  }

  // The item and its marks, centred together.
  int x = std::max(1, (W - group) / 2);
  d.print(x, mid + fit.cap_h / 2, fit.font, TextAlign::BASELINE_LEFT, fr.text.c_str());
  x += tw + 3;
  if (fr.in_use) {
    check_mark(d, x, mid - 4);
    x += 11;
  }
  if (fr.playing)
    play_mark(d, x, mid - 4);

  // Under the item: the caption, or where the item sits in the row.
  const int n = fr.n, item = fr.item;
  if (!fr.cap.empty()) {
    if (width_of(d, f.small, fr.cap) <= W - 2)
      d.print(W / 2, Y_CAPTION, f.small, TextAlign::BASELINE_CENTER, fr.cap.c_str());
    else
      d.print(1, Y_CAPTION, f.small, TextAlign::BASELINE_LEFT, fr.cap.c_str());
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
