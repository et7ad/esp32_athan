#pragma once
// The device menu as two wheels: Up/Down turn the wheel of rows (Radio, Athan, Fajr Athan, ... Info, Exit), Left/Right
// turn the items of the row on screen. The row wheel goes round (Down on the last row is the first); the way back to
// the clock is a row of its own (MenuRow::exit), Select on an item that closes, or the yaml's timeout.
//
// Rows are defined in athan.yaml (script menu_setup), each in one place: its title, its items, and what Left/Right
// and Select do there. This class only keeps the cursor and calls those hooks, so every key takes one path and
// nothing depends on row numbers. It has no ESPHome dependency (psram_alloc.h is plain std:: on a computer) and is
// unit-tested on the host (tests/test_menu.cpp). menu_view.h draws it.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "psram_alloc.h"
#include "tft_theme.h"

namespace esphome {
namespace athan {

/// The five keys of the 5-way switch. The yaml passes them as ints (script ui_key): keep the values.
enum class MenuKey : uint8_t { UP = 0, DOWN = 1, LEFT = 2, RIGHT = 3, SELECT = 4 };

enum class RowKind : uint8_t {
  /// Left/Right show the choices; Select takes the one shown. Select on the one already in use closes the menu.
  CHOICE,
  /// Left/Right change the setting at once (the volumes, Brightness); Select closes the menu.
  VALUE,
  /// Select runs the item shown (play a station, switch a prayer's athan); apply() says whether the menu closes.
  ACTION,
};

/// How a row lays out its items in the menu's right pane (menu_view.cpp). Navigation is the same for all.
enum class RowStyle : uint8_t {
  /// The item large, arrows where Left/Right lead, dots (or a slider) for where it sits.
  LIST,
  /// All items side by side when they fit across the pane (Pre-Fajr: Off On), the one under the cursor highlighted;
  /// otherwise as LIST.
  CHOICES,
  /// A level (volumes): the item large and a bar for where it sits.
  LEVEL,
  /// On/off items (Athan On/Off): the one under the cursor and its state, and every item by the first letter of its
  /// short name with a box under it, filled when on.
  TOGGLES,
  /// Colours (Screen Colour, TFT only): the item's name and a dot in each item's colour (MenuRow::swatch).
  SWATCHES,
};

/// A small icon before a row's short name in the OLED menu's left pane (menu_view.cpp draws it). It groups related
/// rows: a clock for the tick hours, a sun and a moon for the screen's brightness and night.
enum class RowIcon : uint8_t { NONE, CLOCK, SPEAKER, LOCK, EXIT, SUN, MOON };

/// A suggested-list entry as the menu, the /audio page and the web page show it: its number in the list, then its
/// name ("2. Abdul Basit"). `entry` counts from 0. The lists have any number of entries (docs/audio/catalog.json).
inline std::string numbered(int entry, const std::string &name) { return std::to_string(entry + 1) + ". " + name; }

/// Where an item too wide for one line breaks into two (both menu views): the position of a space whose two lines
/// both fit, `width(line)` being a line's width in the font tried and `max_w` the room; std::string::npos when none
/// does (one word, or every split too wide). Of the spaces that fit, one that keeps an entry number ("12.", or the
/// radio's "1") with the first word of the name: "12. Abdul" / "Basit", "123. Mohammed" / "Ali", "1 Cairo" / "Quran";
/// of those the most even split ("Ya Rabbi" / "Ya Rabbi"). The number stands alone ("7." / "Al-Majali") only when it
/// does not fit together with the first word.
template<typename Width> size_t item_break(const std::string &s, Width &&width, int max_w) {
  // An entry number: digits, a dot or not, then a space ("12. Abdul Basit", "1 Cairo Quran"; not "2.5 Hours").
  const size_t digits = s.find_first_not_of("0123456789");
  size_t after_number = std::string::npos;
  if (digits != 0 && digits != std::string::npos) {
    const size_t sp = s[digits] == '.' ? digits + 1 : digits;
    if (sp < s.size() && s[sp] == ' ')
      after_number = sp;
  }
  size_t best = std::string::npos;
  bool best_alone = false;
  int best_w = 0;
  for (size_t sp = s.find(' '); sp != std::string::npos; sp = s.find(' ', sp + 1)) {
    const int w1 = width(s.substr(0, sp)), w2 = width(s.substr(sp + 1)), w = w1 > w2 ? w1 : w2;
    if (w > max_w)
      continue;
    const bool alone = sp == after_number;
    if (best == std::string::npos || (best_alone && !alone) || (alone == best_alone && w < best_w)) {
      best = sp;
      best_alone = alone;
      best_w = w;
    }
  }
  return best;
}

struct MenuRow {
  /// The full name: the right pane's header.
  const char *title{""};
  /// The name in the left pane (the list), shorter where the full one does not fit; nullptr = the title.
  const char *short_title{nullptr};
  RowIcon icon{RowIcon::NONE};
  /// The row's icon on a display with an icon font (the TFT, which gives every row one): a tft_theme.h sym:: glyph.
  const char *symbol{nullptr};
  RowKind kind{RowKind::CHOICE};
  RowStyle style{RowStyle::LIST};
  /// Left/Right go round from the last item to the first. Off for volumes: no jump from 100 % to 0 %.
  bool wrap{true};
  /// The way back to the clock (Exit): Select, Left and Right all close the menu; apply is not called.
  bool exit{false};
  /// Number of items now (0 while a list is still loading). Required.
  std::function<int()> count;
  /// Text of item i. Required.
  std::function<std::string(int)> label;
  /// The item in use, -1 if none: check mark, and where the row opens.
  std::function<int()> active;
  /// The item playing now (the radio station), -1 if none: play mark.
  std::function<int()> playing;
  /// A status line under item i (download progress, what an Info item is), "" for none. With i = -1: why the row
  /// has no items ("Loading list..."). A tab splits it into two lines ("athan.local\tV3.0.0").
  std::function<std::string(int)> caption;
  /// TOGGLES: item i's short name (its first letter is drawn), and whether it is on (a filled box).
  std::function<std::string(int)> short_label;
  std::function<bool(int)> on;
  /// SWATCHES: item i's colour, 0xRRGGBB.
  std::function<uint32_t(int)> swatch;
  /// Stable identity of the item at position i, and the position of an identity now (-1 if gone). With both, the
  /// cursor stays on its item when the list changes under it (a list loads, "Custom" goes after a download).
  /// Without them the cursor keeps its position.
  std::function<int(int)> id_at;
  std::function<int(int)> find;
  /// Item i is now on screen: `entered` when the row was just reached with Up/Down, else after Left/Right.
  std::function<void(int, bool)> show;
  /// CHOICE, ACTION: Select on item i. VALUE: Left/Right reached item i. Returns true to close the menu.
  std::function<bool(int)> apply;
  /// The row is left (Up/Down, or the menu closes): stop what show() started.
  std::function<void()> leave;
};

class Menu {
 public:
  void add_row(MenuRow row) { this->rows_.push_back(std::move(row)); }
  /// Called after the menu closed, whatever closed it (Select, the ends of the wheel, the timeout, the lock).
  void set_on_close(std::function<void()> cb) { this->on_close_ = std::move(cb); }

  bool is_open() const { return this->open_; }
  /// Opens on the first row, on its item in use.
  void open();
  void close();
  void key(MenuKey k);
  /// Keeps the cursor on its item when the row's list changed. key() does it first; call it before drawing.
  void refresh();

  int num_rows() const { return static_cast<int>(this->rows_.size()); }
  int row() const { return this->row_; }
  const MenuRow &row_at(int r) const { return this->rows_[r]; }
  /// Item on screen, -1 while the row has none (yet).
  int item() const { return this->item_; }
  /// Items of the row on screen.
  int count() const;
  /// Neighbours for drawing, -1 for none. A wrapping row shows the other end beside its first and last item only
  /// from 3 items (with 2, the same item would sit on both sides).
  int item_left() const { return this->neighbour_(-1); }
  int item_right() const { return this->neighbour_(1); }
  /// The row `offset` rows from the one on screen, going round (-1: the row above; the last row is above the first).
  int ring_row(int offset) const {
    const int n = this->num_rows();
    return n == 0 ? -1 : ((this->row_ + offset) % n + n) % n;
  }

 protected:
  void enter_(int r);
  void move_(int step);
  void select_();
  void remember_();
  int neighbour_(int step) const;

  PsramVector<MenuRow> rows_;  // about 5 KB of hooks: PSRAM on the device
  std::function<void()> on_close_;
  bool open_{false};
  int row_{0};
  int item_{-1};
  int item_id_{0};  // identity of the item on screen (id_at), so refresh() can find it again
};

}  // namespace athan
}  // namespace esphome
