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

namespace {

// Visits the pixels of a ring's bounding box whose centres lie within a pixel of the band between radii ri and ro:
// visit(x, y, dx, dy, d2), d2 their squared distance from the centre. Row by row, only over the band's two spans: the
// hole and the box's corners are skipped without a square root (the clock's ring, drawn every second, is mostly
// those). The same pixels a scan of the whole box testing (ri - 1)^2 <= d2 <= (ro + 1)^2 would give: each span has a
// pixel of margin, and every pixel still gets that test.
template<typename Visit> void band(float cx, float cy, float ro, float ri, Visit &&visit) {
  const int x0 = static_cast<int>(std::floor(cx - ro - 1)), x1 = static_cast<int>(std::ceil(cx + ro + 1));
  const int y0 = static_cast<int>(std::floor(cy - ro - 1)), y1 = static_cast<int>(std::ceil(cy + ro + 1));
  const float lo = (ri - 1) * (ri - 1), hi = (ro + 1) * (ro + 1);
  for (int y = y0; y <= y1; y++) {
    const float ry = y + 0.5f - cy, ry2 = ry * ry;
    if (ry2 > hi + 4 * (std::fabs(ry) + 1))
      continue;  // the row misses the band, with a margin
    // dx = x + 0.5 - cx: on this row the band lies where xi <= |dx| <= xo
    const float xo = std::sqrt(std::max(0.0f, hi - ry2));
    const int xa = std::max(x0, static_cast<int>(std::floor(cx - 0.5f - xo)) - 2);
    const int xb = std::min(x1, static_cast<int>(std::ceil(cx - 0.5f + xo)) + 2);
    int ha = xb + 1, hb = xb;  // the hole's pixels, skipped: none unless the row crosses it
    if (ry2 + 4 * (std::fabs(ry) + 1) < lo) {
      const float xi = std::sqrt(lo - ry2);
      ha = static_cast<int>(std::ceil(cx - 0.5f - xi)) + 2;
      hb = static_cast<int>(std::floor(cx - 0.5f + xi)) - 2;
    }
    for (int x = xa; x <= xb; x++) {
      if (x >= ha && x <= hb) {
        x = hb;
        continue;
      }
      const float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d2 = dx * dx + dy * dy;
      if (d2 < lo || d2 > hi)
        continue;  // far from the ring: no square root, no angle
      visit(x, y, dx, dy, d2);
    }
  }
}

// How far inside the arc from a0 to a1 (turns; a0 in [0, 1), a1 = a0 + span, span < 1) a ring pixel lies, measured
// along the ring in pixels from its nearer end, as a coverage: 1 well inside, 0 outside, a ramp across the ends. The
// exact angle (atan2) is needed only within two pixels of an end; elsewhere two cross products say inside or out, with
// the same answer: there the ramp is at 1 or 0 anyway (an arc's length from an end is at least the pixel's distance
// from that end's ray).
struct Ends {
  float a0, a1, span, u0x, u0y, u1x, u1y;
  Ends(float a0, float span) : a0(a0), a1(a0 + span), span(span) {
    u0x = std::sin(a0 * TURN), u0y = -std::cos(a0 * TURN);  // the ends' directions: 0 = up, clockwise
    u1x = std::sin(a1 * TURN), u1y = -std::cos(a1 * TURN);
  }
  float along(float dx, float dy, float dist) const {
    constexpr float M = 2.0f;  // pixels from an end within which the exact angle is used
    const float c0 = u0x * dy - u0y * dx;  // > 0: clockwise of the start, |c0| the distance from its line
    const float c1 = dx * u1y - dy * u1x;  // > 0: anticlockwise of the end
    const bool near0 = u0x * dx + u0y * dy > -M && std::fabs(c0) < M;
    const bool near1 = u1x * dx + u1y * dy > -M && std::fabs(c1) < M;
    if (!near0 && !near1) {
      const bool in = span <= 0.5f ? (c0 >= 0 && c1 >= 0) : (c0 >= 0 || c1 >= 0);
      return in ? 1.0f : 0.0f;
    }
    float t = std::atan2(dx, -dy) / TURN;  // 0 at the top, clockwise, in (-0.5, 0.5]
    if (t < a0)
      t += 1.0f;
    if (t < a0 && t + 1.0f <= a1 + 0.5f * (1.0f - span))
      t += 1.0f;  // an arc starting past half a turn that runs past 1.5: its far part (only where it is needed)
    if (t > a1 + 0.5f * (1.0f - span))
      t -= 1.0f;  // nearer the start than the end: measure from the start
    return clamp01(std::min(t - a0, a1 - t) * TURN * dist + 0.5f);
  }
};

}  // namespace

void arc(Display &d, float cx, float cy, float ro, float ri, float a0, float a1, Color c, Color bg, Color side) {
  if (a1 <= a0)
    return;
  const float span = std::min(1.0f, a1 - a0);
  const bool whole = span >= 1.0f;
  a0 -= std::floor(a0);
  const Ends ends(a0, span);
  band(cx, cy, ro, ri, [&](int x, int y, float dx, float dy, float d2) {
    const float dist = std::sqrt(d2);
    const float radial = inside(ro, dist) - inside(ri, dist);
    if (radial <= 0.0f)
      return;
    const float along = whole ? 1.0f : ends.along(dx, dy, dist);  // how far inside the two ends
    if (along <= 0.0f)
      return;
    const Color body = along >= 1.0f ? c : blend(c, side, along);
    plot(d, x, y, radial, body, bg);
  });
}

void ring(Display &d, float cx, float cy, float ro, float ri, float f, Color c, Color track, Color bg) {
  const float span = std::min(1.0f, f);
  const Ends ends(0.0f, span);
  band(cx, cy, ro, ri, [&](int x, int y, float dx, float dy, float d2) {
    const float dist = std::sqrt(d2);
    const float radial = inside(ro, dist) - inside(ri, dist);
    if (radial <= 0.0f)
      return;
    // the filled part where it covers the pixel (its ends blending into the track), the track elsewhere
    const float along = span <= 0.0f ? 0.0f : (span >= 1.0f ? 1.0f : ends.along(dx, dy, dist));
    const Color body = along <= 0.0f ? track : (along >= 1.0f ? c : blend(c, track, along));
    plot(d, x, y, radial, body, bg);
  });
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
