// Host unit tests for the device menu's navigation (components/athan/menu.cpp), no ESP32 needed.
// Run: firmware/tests/run_tests.sh   (builds this with the computer's C++ compiler)

#include <cstdio>
#include <string>
#include <vector>

#include "../components/athan/menu.h"

using namespace esphome::athan;

static int failures = 0;
#define CHECK(cond, ...)                 \
  do {                                   \
    if (!(cond)) {                       \
      failures++;                        \
      std::printf("FAIL: " __VA_ARGS__); \
      std::printf("\n");                 \
    }                                    \
  } while (0)

// What the hooks were called with, in order ("show 0 e" = show(0, entered), "apply 2", "leave", "close").
static std::vector<std::string> calls;
static void log_call(const std::string &s) { calls.push_back(s); }
static bool called(const std::string &s) {
  for (auto &c : calls)
    if (c == s)
      return true;
  return false;
}

// A row whose items live in a vector the test can change; active is a value the test can set.
struct TestRow {
  std::vector<int> ids;  // identity of each position
  int active_id = -1;
  bool apply_closes = false;
};

static MenuRow make_row(const char *title, RowKind kind, TestRow *t, bool wrap = true) {
  MenuRow r;
  r.title = title;
  r.kind = kind;
  r.wrap = wrap;
  r.count = [t]() { return static_cast<int>(t->ids.size()); };
  r.label = [t](int i) { return std::to_string(t->ids[i]); };
  r.active = [t]() {
    for (size_t p = 0; p < t->ids.size(); p++)
      if (t->ids[p] == t->active_id)
        return static_cast<int>(p);
    return -1;
  };
  r.id_at = [t](int i) { return t->ids[i]; };
  r.find = [t](int id) {
    for (size_t p = 0; p < t->ids.size(); p++)
      if (t->ids[p] == id)
        return static_cast<int>(p);
    return -1;
  };
  std::string name = title;
  r.show = [name](int i, bool entered) { log_call(name + " show " + std::to_string(i) + (entered ? " e" : "")); };
  r.apply = [name, t](int i) {
    log_call(name + " apply " + std::to_string(i));
    if (t->ids.size() > static_cast<size_t>(i))
      t->active_id = t->ids[i];
    return t->apply_closes;
  };
  r.leave = [name]() { log_call(name + " leave"); };
  return r;
}

static void test_rows_go_round() {
  TestRow a{{10, 11, 12}, 11}, b{{20, 21}, -1};
  Menu m;
  m.add_row(make_row("A", RowKind::CHOICE, &a));
  m.add_row(make_row("B", RowKind::CHOICE, &b));
  m.set_on_close([]() { log_call("close"); });
  calls.clear();

  m.key(MenuKey::DOWN);
  CHECK(!m.is_open() && calls.empty(), "keys do nothing while closed");

  m.open();
  CHECK(m.is_open() && m.row() == 0 && m.item() == 1, "opens on the first row's item in use (got %d)", m.item());
  CHECK(called("A show 1 e"), "entering a row shows its item");
  CHECK(m.ring_row(-1) == 1 && m.ring_row(1) == 1 && m.ring_row(0) == 0, "the ring: the last row above the first");

  calls.clear();
  m.key(MenuKey::DOWN);
  CHECK(m.row() == 1 && m.item() == 0, "Down: next row, first item when none is in use");
  CHECK(calls.size() == 2 && calls[0] == "A leave" && calls[1] == "B show 0 e", "leave, then show");

  calls.clear();
  m.key(MenuKey::DOWN);
  CHECK(m.is_open() && m.row() == 0, "Down on the last row: the first row, the menu stays open");
  CHECK(calls.size() == 2 && calls[0] == "B leave" && calls[1] == "A show 1 e", "going round leaves and shows");

  calls.clear();
  m.key(MenuKey::UP);
  CHECK(m.is_open() && m.row() == 1 && !called("close"), "Up on the first row: the last row");

  calls.clear();
  m.close();
  CHECK(calls.size() == 2 && calls[0] == "B leave" && calls[1] == "close", "close leaves the row once");
  calls.clear();
  m.close();
  CHECK(calls.empty(), "closing twice does nothing");

  TestRow only{{1, 2}, 1};
  Menu one;
  one.add_row(make_row("O", RowKind::CHOICE, &only));
  one.open();
  calls.clear();
  one.key(MenuKey::UP);
  one.key(MenuKey::DOWN);
  CHECK(one.is_open() && calls.empty(), "a single row: Up/Down do nothing");
}

static void test_exit() {
  // The Exit row: Select, Left and Right all close the menu, without apply().
  TestRow a{{1, 2}, 1}, x{{0}, -1};
  Menu m;
  m.add_row(make_row("A", RowKind::CHOICE, &a));
  MenuRow rx = make_row("X", RowKind::ACTION, &x);
  rx.exit = true;
  m.add_row(rx);
  m.set_on_close([]() { log_call("close"); });
  for (MenuKey k : {MenuKey::SELECT, MenuKey::LEFT, MenuKey::RIGHT}) {
    m.open();
    m.key(MenuKey::UP);  // round: the Exit row sits above the first row
    CHECK(m.row() == 1, "Up from the first row reaches Exit");
    calls.clear();
    m.key(k);
    CHECK(!m.is_open() && !called("X apply 0"), "Exit: key %d closes without applying", static_cast<int>(k));
    CHECK(calls.size() == 2 && calls[0] == "X leave" && calls[1] == "close", "Exit: leave, then close once");
  }
}

static void test_choice() {
  TestRow a{{10, 11, 12, 13}, 11};
  Menu m;
  m.add_row(make_row("A", RowKind::CHOICE, &a));
  m.open();
  CHECK(m.item_left() == 0 && m.item_right() == 2, "neighbours");
  calls.clear();
  m.key(MenuKey::RIGHT);
  m.key(MenuKey::RIGHT);
  CHECK(m.item() == 3 && called("A show 3"), "Right moves and shows");
  CHECK(m.item_right() == 0, "wrapping row: the first item beside the last");
  m.key(MenuKey::RIGHT);
  CHECK(m.item() == 0, "Right wraps");
  m.key(MenuKey::LEFT);
  CHECK(m.item() == 3, "Left wraps");
  CHECK(!called("A apply 3"), "browsing a choice applies nothing");

  calls.clear();
  m.key(MenuKey::SELECT);
  CHECK(m.is_open() && called("A apply 3") && a.active_id == 13, "Select takes the choice and stays");
  calls.clear();
  m.key(MenuKey::SELECT);
  CHECK(!m.is_open() && !called("A apply 3"), "Select on the choice in use closes without applying");

  a.apply_closes = true;
  m.open();
  m.key(MenuKey::LEFT);
  m.key(MenuKey::SELECT);
  CHECK(!m.is_open(), "apply() returning true closes");
}

static void test_value() {
  TestRow v{{0, 1, 2}, 1};
  Menu m;
  m.add_row(make_row("V", RowKind::VALUE, &v, false));
  m.open();
  calls.clear();
  m.key(MenuKey::RIGHT);
  CHECK(m.item() == 2 && called("V apply 2"), "VALUE: Left/Right apply at once");
  calls.clear();
  m.key(MenuKey::RIGHT);
  CHECK(m.item() == 2 && calls.empty(), "no wrap: stops at the end");
  CHECK(m.item_right() == -1 && m.item_left() == 1, "no neighbour past the end");
  m.key(MenuKey::SELECT);
  CHECK(!m.is_open() && !called("V apply 2"), "VALUE: Select closes");

  // The value changed elsewhere (the web page) while the row is open: the row follows it, and the next press
  // steps from there, not from the value it showed before.
  TestRow w{{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, 5};
  Menu n;
  n.add_row(make_row("W", RowKind::VALUE, &w, false));
  n.open();
  w.active_id = 8;
  n.refresh();
  CHECK(n.item() == 8, "VALUE: follows a value set elsewhere (got %d)", n.item());
  calls.clear();
  n.key(MenuKey::RIGHT);
  CHECK(n.item() == 9 && called("W apply 9"), "VALUE: the next press steps from the new value (got %d)", n.item());
}

static void test_action_and_small_rows() {
  TestRow a{{5, 6}, 5}, one{{7}, 7};
  Menu m;
  m.add_row(make_row("A", RowKind::ACTION, &a));
  m.add_row(make_row("O", RowKind::CHOICE, &one));
  m.open();
  CHECK(m.item_left() == -1 && m.item_right() == 1, "2 items: the other one on its own side only");
  calls.clear();
  m.key(MenuKey::SELECT);
  CHECK(m.is_open() && called("A apply 0"), "ACTION: Select applies the item in use too");
  m.key(MenuKey::LEFT);
  CHECK(m.item() == 1, "2 items still wrap with the keys");
  m.key(MenuKey::DOWN);
  calls.clear();
  m.key(MenuKey::RIGHT);
  CHECK(m.item() == 0 && calls.empty() && m.is_open(), "a single item: Left/Right do nothing");
  CHECK(m.item_left() == -1 && m.item_right() == -1, "a single item has no neighbours");
}

static void test_list_changes() {
  TestRow s{{}, 2};  // loading
  Menu m;
  m.add_row(make_row("S", RowKind::CHOICE, &s));
  m.open();
  CHECK(m.item() == -1, "a row still loading has no item");
  calls.clear();
  m.key(MenuKey::RIGHT);
  CHECK(calls.empty(), "Left/Right on an empty row do nothing");

  s.ids = {-1, 0, 1, 2, 3};  // the list arrived: "Custom" (-1) then entries
  m.refresh();
  CHECK(m.item() == 3, "the list arrived: cursor on the item in use (got %d)", m.item());
  CHECK(calls.empty(), "refresh() never calls show()");

  m.key(MenuKey::RIGHT);  // on entry 3, position 4
  s.ids = {0, 1, 2, 3};   // "Custom" gone after a download
  m.refresh();
  CHECK(m.item() == 3, "the cursor follows its item when positions shift (got %d)", m.item());

  s.ids = {0, 1};  // the item vanished: keep the position, inside the list
  m.refresh();
  CHECK(m.item() == 1, "vanished item: clamped to the list (got %d)", m.item());

  s.ids = {};
  m.refresh();
  CHECK(m.item() == -1, "emptied list: no item");
  m.key(MenuKey::SELECT);
  CHECK(!m.is_open(), "Select on an empty row closes");
}

static void test_hook_closes() {
  // A hook that closes the menu itself (the lock) must not leave the cursor half-moved.
  TestRow a{{1, 2}, 1}, b{{3}, 3};
  Menu m;
  MenuRow ra = make_row("A", RowKind::CHOICE, &a);
  ra.leave = [&m]() { log_call("A leave"); m.close(); };
  m.add_row(ra);
  m.add_row(make_row("B", RowKind::CHOICE, &b));
  m.set_on_close([]() { log_call("close"); });
  m.open();
  calls.clear();
  m.key(MenuKey::DOWN);
  CHECK(!m.is_open() && !called("B show 0 e"), "a leave() that closes stops the move");
  int closes = 0;
  for (auto &c : calls)
    closes += c == "close";
  CHECK(closes == 1, "on_close once (got %d)", closes);
}

// A suggested-list entry is shown with its number. A long one breaks into two lines that both fit: the number stays
// with the first word of the name ("12. Abdul" / "Basit") whenever that fits, and stands alone ("7." / "Al-Majali")
// only when it does not. Widths here: 6 px a character.
static void test_entry_labels() {
  CHECK(numbered(0, "Athan 1") == "1. Athan 1", "numbered(0) is \"%s\"", numbered(0, "Athan 1").c_str());
  CHECK(numbered(11, "Abdul Basit") == "12. Abdul Basit", "numbered(11) is \"%s\"", numbered(11, "Abdul Basit").c_str());
  CHECK(numbered(299, "Al-Haddad") == "300. Al-Haddad", "numbered(299)");
  const auto w6 = [](const std::string &s) { return 6 * static_cast<int>(s.size()); };
  const auto brk = [&w6](const char *s, int max_w) { return item_break(s, w6, max_w); };
  const size_t none = std::string::npos;
  CHECK(brk("Abdul", 18) == none, "one word: no break");
  CHECK(brk("Abdul Basit", 30) == 5, "plain name: at the space");
  CHECK(brk("Abdul Basit", 24) == none, "no split fits: no break");
  CHECK(brk("12. Abdul Basit", 54) == 9, "numbered: after the name's first word (got %zu)", brk("12. Abdul Basit", 54));
  CHECK(brk("1. Ya Rabbi", 30) == 5, "numbered: \"1. Ya\" / \"Rabbi\", not \"1.\" alone (got %zu)",
        brk("1. Ya Rabbi", 30));
  CHECK(brk("123. Mohammed Ali", 78) == 13, "3-digit number with the first word (got %zu)",
        brk("123. Mohammed Ali", 78));
  CHECK(brk("12. Abdulrahman A", 80) == 3, "the number alone when it does not fit with the first word (got %zu)",
        brk("12. Abdulrahman A", 80));
  CHECK(brk("7. Al-Majali", 54) == 2, "one-word name: the number alone, the only split");
  CHECK(brk("123. Al-Haddad", 54) == 4, "3-digit number, one-word name: the number alone");
  CHECK(brk("1 Cairo Quran", 42) == 7, "radio label (no dot): \"1 Cairo\" / \"Quran\" (got %zu)",
        brk("1 Cairo Quran", 42));
  CHECK(brk("8 Al-Dosari", 54) == 1, "radio label, one-word name: the number alone");
  CHECK(brk("2.5 Hours", 30) == 3, "a decimal: at its space");
  CHECK(brk("2.5 Ho Hum", 60) == 3, "a decimal is not an entry number: the even split may leave it alone (got %zu)",
        brk("2.5 Ho Hum", 60));
  CHECK(brk("Ya Rabbi Ya Rabbi", 100) == 8, "words only: the most even split (got %zu)", brk("Ya Rabbi Ya Rabbi", 100));
}

int main() {
  test_entry_labels();
  test_rows_go_round();
  test_exit();
  test_choice();
  test_value();
  test_action_and_small_rows();
  test_list_changes();
  test_hook_closes();
  std::printf(failures ? "menu: %d FAILURE(S)\n" : "menu: all tests passed\n", failures);
  return failures ? 1 : 0;
}
