#pragma once
// Draws the device menu (menu.h) on the 128x64 OLED.
//
//       Fajr Athan: Mishary          the row above, small (above the first row: "Home")
//            Tawashih                the row on screen
//   hary   Nasr Eddin v   Afas       the item, large, with a check mark; the items left and right of it, small
//            . . # . .               position in the row (or a caption: download progress, what an Info item is)
//         Pre-Fajr: Off              the row below, small
//
// Small = a pixel font at its native size (Tiny5, 8 px), crisp on the one-colour OLED. Check mark: the item in
// use. Play mark: the radio station playing.

#include <string>

#include "esphome/components/display/display.h"

#include "menu.h"

namespace esphome {
namespace athan {

/// The yaml's fonts. The layout assumes Roboto at 20 and 14 px (cap heights 14, 10) and Tiny5 at 8 px (cap 5).
struct MenuFonts {
  display::BaseFont *large;   // the item
  display::BaseFont *medium;  // the row title, items too wide for the large font
  display::BaseFont *small;   // the previews of the neighbours and the caption (a pixel font)
};

/// Draws the open menu (nothing when it is closed). The caller clears the screen before and shows it after.
void draw_menu(display::Display &d, Menu &menu, const MenuFonts &fonts);

/// Whether the open menu would look different from what draw_menu() last drew (a download's progress, a list that
/// loaded). Reads the same hooks, no display access: the 1 s refresh redraws (about 23 ms of I2C) only then.
bool menu_changed(Menu &menu);

}  // namespace athan
}  // namespace esphome
