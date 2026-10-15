// Host test of the TFT's ring drawing (components/athan/tft_draw.cpp): the fast arc() and ring() draw exactly the
// pixels, in exactly the colours, of a plain scan of the whole box that works out the angle of every ring pixel (the
// reference below, which is the original arc()). The fast ones skip the hole, and the angle wherever a pixel is more
// than two pixels from an arc's end; the clock redraws its ring every second, so that matters on the device.
// Run: firmware/tests/run_tests.sh

#include <cmath>
#include <cstdio>
#include <map>

#include "../components/athan/tft_draw.h"

using namespace esphome;
using display::Display;

namespace reference {

constexpr float TURN = 6.2831853f;
inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float inside(float r, float d) { return clamp01(r - d + 0.5f); }
inline void plot(Display &d, int x, int y, float cover, Color c, Color bg) {
  if (cover <= 0.0f)
    return;
  d.draw_pixel_at(x, y, cover >= 1.0f ? c : athan::tft::blend(c, bg, cover));
}

void arc(Display &d, float cx, float cy, float ro, float ri, float a0, float a1, Color c, Color bg, Color side) {
  if (a1 <= a0)
    return;
  const float span = std::fmin(1.0f, a1 - a0);
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
        continue;
      const float dist = std::sqrt(d2);
      const float radial = inside(ro, dist) - inside(ri, dist);
      if (radial <= 0.0f)
        continue;
      float along = 1.0f;
      if (!whole) {
        float t = std::atan2(dx, -dy) / TURN;
        if (t < a0)
          t += 1.0f;
        if (t < a0 && t + 1.0f <= a1 + 0.5f * (1.0f - span))
          t += 1.0f;
        if (t > a1 + 0.5f * (1.0f - span))
          t -= 1.0f;
        along = clamp01(std::fmin(t - a0, a1 - t) * TURN * dist + 0.5f);
        if (along <= 0.0f)
          continue;
      }
      const Color body = along >= 1.0f ? c : athan::tft::blend(c, side, along);
      plot(d, x, y, radial, body, bg);
    }
  }
}

}  // namespace reference

struct Canvas : Display {
  std::map<std::pair<int, int>, uint32_t> px;
  void draw_pixel_at(int x, int y, Color c) override {
    px[{x, y}] = (uint32_t(c.r) << 24) | (uint32_t(c.g) << 16) | (uint32_t(c.b) << 8) | c.w;
  }
};

int main() {
  const Color c(230, 160, 40), bg(0, 0, 0), track(55, 60, 66);
  const float centres[][2] = {{120, 114}, {120.5f, 114.3f}, {60.25f, 40.75f}, {7.9f, 200.1f}};
  const float radii[][2] = {{64, 55}, {8, 4}, {30.5f, 20.2f}, {100, 88}, {3, 0.5f}};
  const float starts[] = {0, 0.1f, 0.25f, 0.37f, 0.5f, 0.75f, 0.99f, -0.2f, 1.3f, 0.123456f};
  const float spans[] = {0.0005f, 0.001f, 0.01f, 0.1f, 0.25f, 0.33f, 0.49f, 0.4999f, 0.5f, 0.5001f, 0.51f, 0.75f,
                         0.9f, 0.99f, 0.9999f, 1.0f, 1.5f};
  int cases = 0, failures = 0;
  for (auto &ce : centres)
    for (auto &ra : radii) {
      for (float a0 : starts)
        for (float sp : spans) {
          Canvas want, got;
          reference::arc(want, ce[0], ce[1], ra[0], ra[1], a0, a0 + sp, c, bg, track);
          athan::tft::arc(got, ce[0], ce[1], ra[0], ra[1], a0, a0 + sp, c, bg, track);
          cases++;
          if (want.px != got.px && failures++ < 5)
            std::printf("FAIL: arc centre %.2f,%.2f radii %.1f/%.1f from %.4f span %.4f: %zu pixels, want %zu\n",
                        ce[0], ce[1], ra[0], ra[1], a0, sp, got.px.size(), want.px.size());
        }
      // ring(f) is the track drawn whole, then the filled part over it
      for (int k = 0; k <= 120; k++) {
        const float f = k == 120 ? 1.2f : k / 119.0f;
        Canvas want, got;
        reference::arc(want, ce[0], ce[1], ra[0], ra[1], 0, 1, track, bg, bg);
        reference::arc(want, ce[0], ce[1], ra[0], ra[1], 0, f, c, bg, track);
        athan::tft::ring(got, ce[0], ce[1], ra[0], ra[1], f, c, track, bg);
        cases++;
        if (want.px != got.px && failures++ < 10)
          std::printf("FAIL: ring centre %.2f,%.2f radii %.1f/%.1f filled %.5f\n", ce[0], ce[1], ra[0], ra[1], f);
      }
    }
  std::printf(failures ? "tft draw: %d of %d FAILED\n" : "tft draw: all %d rings and arcs pixel-identical\n",
              failures ? failures : cases, cases);
  return failures ? 1 : 0;
}
