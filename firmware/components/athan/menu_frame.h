#pragma once
// What the open menu shows, read from the rows' hooks once per draw. Both displays' drawings (menu_view.cpp for the
// OLED, menu_view_tft.cpp for the TFT) draw from it, and its fingerprint tells whether a redraw would change anything
// (menu_changed()).

#include <cstdint>
#include <string>
#include <vector>

#include "menu.h"

namespace esphome {
namespace athan {

struct Frame {
  int row{0};
  const char *title{""};
  RowKind kind{RowKind::CHOICE};
  RowStyle style{RowStyle::LIST};
  bool exit{false};
  std::string text, cap;
  int item{-1}, n{0}, active{-1};
  bool in_use{false}, playing{false}, has_left{false}, has_right{false};
  std::vector<std::string> all;     // CHOICES: every item, side by side when they fit
  std::vector<std::string> shorts;  // TOGGLES: every item's short name
  std::vector<bool> ons;            // TOGGLES: on or off
  std::vector<uint32_t> swatches;   // SWATCHES: every item's colour
};

const int MAX_SIDE_BY_SIDE = 6;  // at most this many items in a CHOICES, TOGGLES or SWATCHES line

/// The row on screen as the menu would draw it now (refreshes the cursor first).
Frame frame_of(Menu &menu);
/// Remembers what was drawn, for menu_changed(). The drawing calls it with the frame it drew.
void frame_drawn(const Frame &fr);

}  // namespace athan
}  // namespace esphome
