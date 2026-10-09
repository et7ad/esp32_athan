#pragma once
// Draws the device menu (menu.h) on the 128x64 OLED as two panes.
//
//    Athan       |     FAJR ATHAN         the left pane: five rows by short name (MenuRow::short_title) in the medium
//  [ Fajr      ] |  <    Abdul    >       font, after an optional icon (RowIcon: a clock for the tick rows, a speaker
//    Tawsh       |       Basit            for the sound and volume rows, a lock, the exit arrow); a ring (Up/Down go
//    Pre-Fajr    |         v              round), the row on screen always in the middle, highlighted; a dotted line
//  (clock) Tick  |   . # . . . . . .      where the list starts again
//                                         the right pane: the row's full title small at the top, then its item as large
//                                         as it fits (two lines of the medium font when one is too narrow), arrows where
//                                         Left/Right lead, the check mark (in use) and play mark (playing) under it, then
//                                         dots, a level bar or the caption (small font, up to two lines)
//
// Other row styles (menu.h RowStyle) in the right pane: CHOICES side by side when they fit (Off  On), TOGGLES as the
// prayer and its state with every prayer's initial and a box (filled = on) under them, LEVEL with a bar. A single item
// is a button for Select (Update, Lock), or with a tab in its caption the label and the caption's two parts on lines
// of their own (Info: the IP address in MenuFonts::numbers when the medium font is too wide). The exit row
// (MenuRow::exit) previews the clock: its label (the time), its caption (the next prayer), and which keys go back.
//
// Small = a pixel font at its native size (Tiny5, 8 px), crisp on the one-colour OLED.

#include <string>

#include "esphome/components/display/display.h"

#include "menu.h"

namespace esphome {
namespace athan {

/// The yaml's fonts. The layout assumes Roboto at 20 and 14 px (cap heights 14, 10), Tiny5 at 8 px (cap 5) and
/// Roboto at 11 px for the numbers (cap 8).
struct MenuFonts {
  display::BaseFont *large;   // the item
  display::BaseFont *medium;  // the rows' names, items too wide for the large font
  display::BaseFont *small;   // the right pane's title, the caption (a pixel font)
  display::BaseFont *numbers{nullptr};  // digits and '.' only, between small and medium: Info's IP address
};

/// Draws the open menu (nothing when it is closed). The caller clears the screen before and shows it after.
void draw_menu(display::Display &d, Menu &menu, const MenuFonts &fonts);

/// Whether the open menu would look different from what draw_menu() last drew (a download's progress, a list that
/// loaded). Reads the same hooks, no display access: the 1 s refresh redraws (about 23 ms of I2C) only then.
bool menu_changed(Menu &menu);

}  // namespace athan
}  // namespace esphome
