#pragma once
// The device menu as two wheels: Up/Down turn the wheel of rows (Radio, Athan, Fajr Athan, ... Info), Left/Right
// turn the items of the row on screen. Past either end of the row wheel is the clock: the menu closes.
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

namespace esphome {
namespace athan {

/// The five keys of the 5-way switch. The yaml passes them as ints (script ui_key): keep the values.
enum class MenuKey : uint8_t { UP = 0, DOWN = 1, LEFT = 2, RIGHT = 3, SELECT = 4 };

enum class RowKind : uint8_t {
  /// Left/Right show the choices; Select takes the one shown. Select on the one already in use closes the menu.
  CHOICE,
  /// Left/Right change the setting at once (volume, hours); Select closes the menu.
  VALUE,
  /// Select runs the item shown (play a station, switch a prayer's athan); apply() says whether the menu closes.
  ACTION,
};

struct MenuRow {
  const char *title{""};
  RowKind kind{RowKind::CHOICE};
  /// Left/Right go round from the last item to the first. Off for volumes: no jump from 100 % to 0 %.
  bool wrap{true};
  /// Number of items now (0 while a list is still loading). Required.
  std::function<int()> count;
  /// Text of item i. Required.
  std::function<std::string(int)> label;
  /// The item in use, -1 if none: check mark, and where the row opens.
  std::function<int()> active;
  /// The item playing now (the radio station), -1 if none: play mark.
  std::function<int()> playing;
  /// A status line under item i (download progress, what an Info item is), "" for none. With i = -1: why the row
  /// has no items ("Loading list...").
  std::function<std::string(int)> caption;
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
  /// Rows above and below; -1 past the ends, which is the clock.
  int row_above() const { return this->row_ - 1; }
  int row_below() const { return this->row_ + 1 < this->num_rows() ? this->row_ + 1 : -1; }

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
