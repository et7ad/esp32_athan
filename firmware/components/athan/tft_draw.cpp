#include "tft_draw.h"

#include <algorithm>
#include <cmath>

namespace esphome {
namespace athan {
namespace tft {

using display::Display;
using display::TextAlign;

namespace {

constexpr float TURN = 6.2831853f;

inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// How much of the pixel whose centre is d from a circle's centre lies inside radius r (a one-pixel ramp).
inline float inside(float r, float d) { return clamp01(r - d + 0.5f); }

inline void plot(Display &d, int x, int y, float cover, Color c, Color bg) {
  if (cover <= 0.0f)
    return;
  d.draw_pixel_at(x, y, cover >= 1.0f ? c : blend(c, bg, cover));
}

}  // namespace

Color blend(Color a, Color b, float t) {
  t = clamp01(t);
  auto mix = [t](uint8_t p, uint8_t q) { return static_cast<uint8_t>(q + (p - q) * t + 0.5f); };
  return Color(mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.w, b.w));
}

void fill_round_rect(Display &d, int x, int y, int w, int h, int r, Color c, Color bg) {
  r = std::max(0, std::min(r, std::min(w, h) / 2));
  d.filled_rectangle(x + r, y, w - 2 * r, h, c);
  d.filled_rectangle(x, y + r, r, h - 2 * r, c);
  d.filled_rectangle(x + w - r, y + r, r, h - 2 * r, c);
  // the four corners, pixel by pixel round the centres of their quarter circles
  for (int j = 0; j < r; j++) {
    for (int i = 0; i < r; i++) {
      const float dx = r - (i + 0.5f), dy = r - (j + 0.5f);
      const float cover = inside(static_cast<float>(r), std::sqrt(dx * dx + dy * dy));
      plot(d, x + i, y + j, cover, c, bg);
      plot(d, x + w - 1 - i, y + j, cover, c, bg);
      plot(d, x + i, y + h - 1 - j, cover, c, bg);
      plot(d, x + w - 1 - i, y + h - 1 - j, cover, c, bg);
    }
  }
}

void round_rect_frame(Display &d, int x, int y, int w, int h, int r, int t, Color c, Color bg) {
  r = std::max(t, std::min(r, std::min(w, h) / 2));
  d.filled_rectangle(x + r, y, w - 2 * r, t, c);
  d.filled_rectangle(x + r, y + h - t, w - 2 * r, t, c);
  d.filled_rectangle(x, y + r, t, h - 2 * r, c);
  d.filled_rectangle(x + w - t, y + r, t, h - 2 * r, c);
  const float ro = static_cast<float>(r), ri = static_cast<float>(r - t);
  for (int j = 0; j < r; j++) {
    for (int i = 0; i < r; i++) {
      const float dx = r - (i + 0.5f), dy = r - (j + 0.5f), dist = std::sqrt(dx * dx + dy * dy);
      const float cover = inside(ro, dist) - inside(ri, dist);
      plot(d, x + i, y + j, cover, c, bg);
      plot(d, x + w - 1 - i, y + j, cover, c, bg);
      plot(d, x + i, y + h - 1 - j, cover, c, bg);
      plot(d, x + w - 1 - i, y + h - 1 - j, cover, c, bg);
    }
  }
}

void fill_circle(Display &d, float cx, float cy, float r, Color c, Color bg) {
  const int x0 = static_cast<int>(std::floor(cx - r - 1)), x1 = static_cast<int>(std::ceil(cx + r + 1));
  const int y0 = static_cast<int>(std::floor(cy - r - 1)), y1 = static_cast<int>(std::ceil(cy + r + 1));
  for (int y = y0; y <= y1; y++) {
    for (int x = x0; x <= x1; x++) {
      const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
      plot(d, x, y, inside(r, std::sqrt(dx * dx + dy * dy)), c, bg);
    }
  }
}

void circle_frame(Display &d, float cx, float cy, float r, float t, Color c, Color bg) {
  const float ro = r + t / 2, ri = r - t / 2;
  const int x0 = static_cast<int>(std::floor(cx - ro - 1)), x1 = static_cast<int>(std::ceil(cx + ro + 1));
  const int y0 = static_cast<int>(std::floor(cy - ro - 1)), y1 = static_cast<int>(std::ceil(cy + ro + 1));
  for (int y = y0; y <= y1; y++) {
    for (int x = x0; x <= x1; x++) {
      const float dx = x + 0.5f - cx, dy = y + 0.5f - cy, dist = std::sqrt(dx * dx + dy * dy);
      plot(d, x, y, inside(ro, dist) - inside(ri, dist), c, bg);
    }
  }
}

void arc(Display &d, float cx, float cy, float ro, float ri, float a0, float a1, Color c, Color bg, Color side) {
  if (a1 <= a0)
    return;
  const float span = std::min(1.0f, a1 - a0);
  const bool whole = span >= 1.0f;
  a0 -= std::floor(a0);
  a1 = a0 + span;
  const int x0 = static_cast<int>(std::floor(cx - ro - 1)), x1 = static_cast<int>(std::ceil(cx + ro + 1));
  const int y0 = static_cast<int>(std::floor(cy - ro - 1)), y1 = static_cast<int>(std::ceil(cy + ro + 1));
  const float lo = (ri - 1) * (ri - 1), hi = (ro + 1) * (ro + 1);
  for (int y = y0; y <= y1; y++) {
    for (int x = x0; x <= x1; x++) {
      const float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d2 = dx * dx + dy * dy;
      if (d2 < lo || d2 > hi)
        continue;  // far from the ring: no square root, no angle
      const float dist = std::sqrt(d2);
      const float radial = inside(ro, dist) - inside(ri, dist);
      if (radial <= 0.0f)
        continue;
      float along = 1.0f;  // how far inside the two ends, in pixels along the ring
      if (!whole) {
        float t = std::atan2(dx, -dy) / TURN;  // 0 at the top, clockwise
        if (t < a0)
          t += 1.0f;
        if (t > a1 + 0.5f * (1.0f - span))
          t -= 1.0f;  // nearer the start than the end: measure from the start
        along = clamp01(std::min(t - a0, a1 - t) * TURN * dist + 0.5f);
        if (along <= 0.0f)
          continue;
      }
      const Color body = along >= 1.0f ? c : blend(c, side, along);
      plot(d, x, y, radial, body, bg);
    }
  }
}

void icon(Display &d, int x, int y, display::BaseFont *font, int size, const char *glyph, Color c, Color bg) {
  d.print(x, y + size, font, c, TextAlign::BASELINE_LEFT, glyph, bg);
}

int text_width(Display &d, display::BaseFont *font, const char *s) {
  int x1, y1, w, h;
  d.get_text_bounds(0, 0, s, font, TextAlign::TOP_LEFT, &x1, &y1, &w, &h);
  return w;
}

}  // namespace tft
}  // namespace athan
}  // namespace esphome
