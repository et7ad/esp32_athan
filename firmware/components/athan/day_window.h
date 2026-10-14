#pragma once
// A window of the day [from, until) for the hourly tick and the screen's night. Each end is an hour (0-23) or a prayer
// (WINDOW_PRAYER + its index in the day: Fajr 0, Dhuhr 3, Maghrib 5, Isha 6), which moves with the prayer times every
// day: "from Isha until Fajr" follows the seasons by itself. The menu and the web page offer, in this order:
//   from:  Maghrib, Isha, 00:00 ... 23:00
//   until: Fajr, Dhuhr, 00:00 ... 23:00
// Settings saved before the prayer ends existed (plain hours) keep their meaning. No ESPHome dependency (host-tested).

#include <cstdint>

namespace esphome {
namespace athan {

constexpr int WINDOW_PRAYER = 100;
constexpr int WINDOW_CHOICES = 26;  // two prayers, then the 24 hours

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
inline int window_value(bool until, int i) {
  if (i == 0)
    return WINDOW_PRAYER + (until ? 0 : 5);  // Fajr / Maghrib
  if (i == 1)
    return WINDOW_PRAYER + (until ? 3 : 6);  // Dhuhr / Isha
  return i - 2 < 0 || i - 2 > 23 ? 0 : i - 2;
}
inline int window_choice(bool until, int v) {
  if (v == window_value(until, 0))
    return 0;
  if (v == window_value(until, 1))
    return 1;
  if (v >= 0 && v <= 23)
    return v + 2;
  return 0;  // a prayer this end does not offer: show the first one
}

/// A prayer end's name, or nullptr for an hour (the caller formats it, 24- or 12-hour).
inline const char *window_prayer_name(int v) {
  static const char *const names[7] = {"Fajr", "Sunrise", "Doha", "Dhuhr", "Asr", "Maghrib", "Isha"};
  return v >= WINDOW_PRAYER && v - WINDOW_PRAYER < 7 ? names[v - WINDOW_PRAYER] : nullptr;
}

}  // namespace athan
}  // namespace esphome
