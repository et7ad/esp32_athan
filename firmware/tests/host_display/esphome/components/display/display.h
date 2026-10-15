#pragma once
// Host stand-in for ESPHome's display.h (test_tft_draw.cpp only): just what components/athan/tft_draw.cpp uses. The
// test's Display records every pixel drawn; text is not drawn.
#include <cstdint>

namespace esphome {
struct Color {
  uint8_t r{0}, g{0}, b{0}, w{0};
  Color() = default;
  Color(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) : r(r), g(g), b(b), w(w) {}
};
namespace display {
enum class TextAlign { TOP_LEFT, BASELINE_LEFT, BASELINE_CENTER };
class BaseFont {};
static const Color COLOR_OFF(0, 0, 0, 0);
class Display {
 public:
  virtual ~Display() = default;
  virtual void draw_pixel_at(int x, int y, Color c) = 0;
  void filled_rectangle(int x, int y, int w, int h, Color c) {
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++)
        this->draw_pixel_at(x + i, y + j, c);
  }
  void print(int, int, BaseFont *, Color, TextAlign, const char *, Color = COLOR_OFF) {}
  void get_text_bounds(int, int, const char *, BaseFont *, TextAlign, int *x1, int *y1, int *w, int *h) {
    *x1 = *y1 = *w = *h = 0;
  }
};
}  // namespace display
}  // namespace esphome
