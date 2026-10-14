#pragma once
// Drawing helpers for the colour TFT (displays/tft.yaml, menu_view_tft.cpp). ESPHome's Display has rectangles,
// circles and full rings, nothing rounded or partial, and no smoothing for shapes. These smooth their edges by
// blending into the colour behind them, which the caller names (the panel cannot be read back): `bg`.
//
// Text over a colour needs the same: pass that colour as print()'s `background`, or the smoothed edges of the
// letters blend into black.

#include "esphome/components/display/display.h"

namespace esphome {
namespace athan {
namespace tft {

/// a where t = 1, b where t = 0.
Color blend(Color a, Color b, float t);

void fill_round_rect(display::Display &d, int x, int y, int w, int h, int r, Color c, Color bg);
/// A frame t pixels thick inside the box.
void round_rect_frame(display::Display &d, int x, int y, int w, int h, int r, int t, Color c, Color bg);
void fill_circle(display::Display &d, float cx, float cy, float r, Color c, Color bg);
/// A circle's outline t pixels thick, centred on radius r.
void circle_frame(display::Display &d, float cx, float cy, float r, float t, Color c, Color bg);
/// The part of the ring between radii ri and ro from a0 to a1, in turns: 0 = the top, clockwise (a1 - a0 >= 1 is the
/// whole ring). Its inner and outer edges blend into `bg`, its two ends into `side` (the ring drawn under it).
void arc(display::Display &d, float cx, float cy, float ro, float ri, float a0, float a1, Color c, Color bg,
         Color side);

/// An icon (a glyph of an icon font of `size` px) with its square's top-left at x, y. Material Symbols puts the
/// square on the baseline, so this prints at baseline y + size.
void icon(display::Display &d, int x, int y, display::BaseFont *font, int size, const char *glyph, Color c,
          Color bg = display::COLOR_OFF);
int text_width(display::Display &d, display::BaseFont *font, const char *s);

}  // namespace tft
}  // namespace athan
}  // namespace esphome
