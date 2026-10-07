#pragma once
// Draws the device menu (menu.h) on the 128x64 OLED.
//
//      (Fajr Athan: Mishary)         the row above, faded (above the first row: "Home")
//            Tawashih                the row on screen
//   (hary)  Nasr Eddin v  (Afas)     the item, large, with a check mark; the items left and right of it, faded
//            . . # . .               position in the row (or a caption: download progress, what an Info item is)
//         (Pre-Fajr: Off)            the row below, faded
//
// Faded = one pixel in four cleared (the OLED has one colour). Check mark: the item in use. Play mark: the radio
// station playing.

#include <string>

#include "esphome/components/display/display.h"

#include "menu.h"

namespace esphome {
namespace athan {

/// The yaml's fonts. The layout assumes Roboto at 20, 14 and 10 px (cap heights 14, 10, 7).
struct MenuFonts {
  display::BaseFont *large;   // the item
  display::BaseFont *medium;  // the row title, items too wide for the large font
  display::BaseFont *small;   // the faded previews and the caption
};

/// Draws the open menu (nothing when it is closed). The caller clears the screen before and shows it after.
void draw_menu(display::Display &d, Menu &menu, const MenuFonts &fonts);

/// Whether the open menu would look different from what draw_menu() last drew (a download's progress, a list that
/// loaded). Reads the same hooks, no display access: the 1 s refresh redraws (about 23 ms of I2C) only then.
bool menu_changed(Menu &menu);

}  // namespace athan
}  // namespace esphome
