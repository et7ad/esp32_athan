#pragma once
// Draws the device menu (menu.h) on the 128x64 OLED as two panes.
//
//    Info          |                     the left pane: the rows by name, a ring (Up/Down go round), the row on screen
//   < Exit         |       Abdul         always in the middle, highlighted; a dotted line where the list starts again
//   . . . . . . .  |  <    Basit    >    the right pane: the row's item, as large as it fits (two lines of the medium
//  [ Athan       ] |        v            font when one is too narrow), arrows where Left/Right lead, the check mark
//    Fajr Athan    |   . # . . . . .     (in use) and play mark (playing) under it, then dots, a level bar or the
//    Tawashih      |                     caption (small font, up to two lines)
//    Pre-Fajr      |
//
// Other row styles (menu.h RowStyle) in the right pane: CHOICES side by side when they fit (Off  On), TOGGLES as the
// prayer and its state with every prayer's initial and a box (filled = on) under them, LEVEL with a bar. A single item
// is a button for Select (Update, Lock), or with a tab in its caption the label and the caption's two parts on lines
// of their own (Info). The exit row (MenuRow::exit) previews the clock: its label (the time), its caption (the next
// prayer), and which keys go back.
//
// Small = a pixel font at its native size (Tiny5, 8 px), crisp on the one-colour OLED.

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
