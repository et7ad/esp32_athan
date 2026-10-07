#include "menu.h"

namespace esphome {
namespace athan {

int Menu::count() const {
  if (!this->open_ || this->rows_.empty())
    return 0;
  const MenuRow &row = this->rows_[this->row_];
  const int n = row.count ? row.count() : 0;
  return n > 0 ? n : 0;
}

void Menu::open() {
  if (this->open_ || this->rows_.empty())
    return;
  this->open_ = true;
  this->enter_(0);
}

void Menu::close() {
  if (!this->open_)
    return;
  this->open_ = false;  // first, so a hook that closes again does nothing
  const MenuRow &row = this->rows_[this->row_];
  if (row.leave)
    row.leave();
  if (this->on_close_)
    this->on_close_();
}

void Menu::key(MenuKey k) {
  if (!this->open_)
    return;
  this->refresh();
  switch (k) {
    case MenuKey::UP:
    case MenuKey::DOWN: {
      const int r = this->row_ + (k == MenuKey::UP ? -1 : 1);
      if (r < 0 || r >= this->num_rows()) {
        this->close();  // past the first or the last row: back to the clock
        return;
      }
      const MenuRow &old = this->rows_[this->row_];
      if (old.leave)
        old.leave();
      if (this->open_)
        this->enter_(r);
      break;
    }
    case MenuKey::LEFT:
      this->move_(-1);
      break;
    case MenuKey::RIGHT:
      this->move_(1);
      break;
    case MenuKey::SELECT:
      this->select_();
      break;
  }
}

void Menu::refresh() {
  if (!this->open_)
    return;
  const MenuRow &row = this->rows_[this->row_];
  const int n = this->count();
  if (n == 0) {
    this->item_ = -1;
    return;
  }
  if (this->item_ < 0) {
    // The list arrived while the row was on screen: show the item in use. No show(): drawing calls refresh(),
    // and a preview must only start from a key.
    const int a = row.active ? row.active() : -1;
    this->item_ = (a >= 0 && a < n) ? a : 0;
    this->remember_();
    return;
  }
  if (row.id_at && row.find) {
    const int p = row.find(this->item_id_);
    if (p >= 0 && p < n) {
      this->item_ = p;
      return;
    }
  }
  if (this->item_ >= n)
    this->item_ = n - 1;
  this->remember_();
}

void Menu::enter_(int r) {
  this->row_ = r;
  const MenuRow &row = this->rows_[r];
  const int n = this->count();
  const int a = row.active ? row.active() : -1;
  this->item_ = n == 0 ? -1 : ((a >= 0 && a < n) ? a : 0);
  this->remember_();
  if (this->item_ >= 0 && row.show)
    row.show(this->item_, true);
}

void Menu::move_(int step) {
  const MenuRow &row = this->rows_[this->row_];
  const int n = this->count();
  if (n == 0)
    return;
  int j = (this->item_ < 0 ? 0 : this->item_) + step;
  if (j < 0 || j >= n) {
    if (!row.wrap)
      return;  // stops at the ends
    j = (j + n) % n;
  }
  if (j == this->item_)
    return;  // a single item
  this->item_ = j;
  this->remember_();
  if (row.kind == RowKind::VALUE && row.apply && row.apply(j)) {
    this->close();
    return;
  }
  if (this->open_ && row.show)
    row.show(j, false);
}

void Menu::select_() {
  const MenuRow &row = this->rows_[this->row_];
  if (row.kind == RowKind::VALUE || this->item_ < 0) {
    this->close();  // nothing to take: Select means done
    return;
  }
  if (row.kind == RowKind::CHOICE && row.active && row.active() == this->item_) {
    this->close();  // already in use
    return;
  }
  if (!row.apply || row.apply(this->item_)) {
    this->close();
    return;
  }
  this->refresh();  // the list may have changed (an entry taken, "Custom" gone)
}

void Menu::remember_() {
  const MenuRow &row = this->rows_[this->row_];
  this->item_id_ = (row.id_at && this->item_ >= 0) ? row.id_at(this->item_) : this->item_;
}

int Menu::neighbour_(int step) const {
  const int n = this->count();
  if (this->item_ < 0 || n < 2)
    return -1;
  const int j = this->item_ + step;
  if (j >= 0 && j < n)
    return j;
  if (!this->rows_[this->row_].wrap || n < 3)
    return -1;
  return (j + n) % n;
}

}  // namespace athan
}  // namespace esphome
