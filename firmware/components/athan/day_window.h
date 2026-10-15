#pragma once
// A window of the day [from, until) for the hourly tick and the screen's night. Each end is an hour (0-23) or a prayer
// (WINDOW_PRAYER + its index in the day: Fajr 0, Dhuhr 3, Maghrib 5, Isha 6), which moves with the prayer times every
// day: "from Isha until Fajr" follows the seasons by itself. The two windows offer the same prayers at opposite ends,
// in this order in the menu and on the web page:
//   the screen's night  from:  Maghrib, Isha, 00:00 ... 23:00    until: Fajr, Dhuhr, 00:00 ... 23:00
//   the hourly tick     from:  Fajr, Dhuhr, 00:00 ... 23:00      until: Maghrib, Isha, 00:00 ... 23:00
// Settings saved before the prayer ends existed (plain hours) keep their meaning; an end a window no longer offers
// becomes an hour (window_sanitize()). No ESPHome dependency (host-tested).

#include <cstdint>

namespace esphome {
namespace athan {

constexpr int WINDOW_PRAYER = 100;
constexpr int WINDOW_CHOICES = 26;  // two prayers, then the 24 hours

/// Which window: the hourly tick's (by day: from Fajr or Dhuhr until Maghrib or Isha) or the screen's night (from
/// Maghrib or Isha until Fajr or Dhuhr).
enum class WindowKind : uint8_t { TICK, NIGHT };

/// The minute of the day an end stands for, with today's prayer times (prayer_hours / prayer_minutes, 7 each).
inline int window_minute(int v, const uint8_t *hours, const uint8_t *minutes) {
  if (v >= WINDOW_PRAYER) {
    const int pi = v - WINDOW_PRAYER;
    if (pi >= 0 && pi < 7)
      return hours[pi] * 60 + minutes[pi];
    return 0;
  }
  return (v < 0 || v > 23 ? 0 : v) * 60;
}

/// Whether minute t of the day lies in [from, until); both ends at the same minute = all day.
inline bool in_window(int from, int until, int t, const uint8_t *hours, const uint8_t *minutes) {
  const int a = window_minute(from, hours, minutes), b = window_minute(until, hours, minutes);
  if (a == b)
    return true;
  return a < b ? (t >= a && t < b) : (t >= a || t < b);
}

/// The window's length in minutes (1440 for all day).
inline int window_length(int from, int until, const uint8_t *hours, const uint8_t *minutes) {
  const int a = window_minute(from, hours, minutes), b = window_minute(until, hours, minutes);
  return a == b ? 1440 : (b - a + 1440) % 1440;
}

/// The setting for choice i of the menu or the web select (the order above), and the choice a setting shows as.
inline int window_value(WindowKind k, bool until, int i) {
  if (i == 0 || i == 1) {
    const bool dawn = (k == WindowKind::NIGHT) == until;  // the night ends at dawn, the tick's day starts there
    return WINDOW_PRAYER + (dawn ? (i == 0 ? 0 : 3) : (i == 0 ? 5 : 6));  // Fajr, Dhuhr / Maghrib, Isha
  }
  return i - 2 < 0 || i - 2 > 23 ? 0 : i - 2;
}
inline int window_choice(WindowKind k, bool until, int v) {
  if (v == window_value(k, until, 0))
    return 0;
  if (v == window_value(k, until, 1))
    return 1;
  if (v >= 0 && v <= 23)
    return v + 2;
  return -1;  // a prayer this end does not offer: window_sanitize() turns it into an hour at boot
}

/// A saved end this window offers, as it is; one it does not (a prayer from the other window's ends, saved by an
/// earlier firmware) becomes the first whole hour at or after that prayer today, which keeps the hourly ticks the
/// window had: "from Isha (19:53)" ticked first at 20:00, "until Dhuhr (12:58)" last at 12:00.
inline int window_sanitize(WindowKind k, bool until, int v, const uint8_t *hours, const uint8_t *minutes) {
  if (window_choice(k, until, v) >= 0)
    return v;
  if (v >= WINDOW_PRAYER && v - WINDOW_PRAYER < 7)
    return ((window_minute(v, hours, minutes) + 59) / 60) % 24;
  return 0;
}

/// Screen at Night, the saved setting (screen_night): what the screen does inside the night window.
constexpr int NIGHT_NORMAL = 0;  // "Normal (same as day)": the night window changes nothing
constexpr int NIGHT_DIM = 1;     // "Dim"
constexpr int NIGHT_OFF = 2;     // "Turned off": dark (a key still lights it dim for a while: screen_refresh)

/// The screen's level (0-100 %, 0 = dark) inside the night window, from the day's level (Brightness) and the dim level.
/// Normal returns the day's level itself, 0 % included, so a clock set to Normal looks at night exactly as by day.
inline int night_level(int mode, int bright, int dim) {
  if (mode == NIGHT_OFF)
    return 0;
  if (mode == NIGHT_DIM)
    return bright < dim ? bright : dim;
  return bright;  // NIGHT_NORMAL, and anything unknown
}

/// A prayer end's name, or nullptr for an hour (the caller formats it, 24- or 12-hour).
inline const char *window_prayer_name(int v) {
  static const char *const names[7] = {"Fajr", "Sunrise", "Doha", "Dhuhr", "Asr", "Maghrib", "Isha"};
  return v >= WINDOW_PRAYER && v - WINDOW_PRAYER < 7 ? names[v - WINDOW_PRAYER] : nullptr;
}

}  // namespace athan
}  // namespace esphome
