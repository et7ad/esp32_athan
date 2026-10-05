#pragma once
// POSIX time zone strings ("PST8PDT,M3.2.0,M11.1.0") and calendar helpers.
//
// Plain C++ with no ESPHome or ESP-IDF includes, so it can be unit-tested on a computer
// (firmware/tests/test_helpers.cpp). ESPHome 2026.x only parses time zones at build time, while version 3 must
// switch zones at runtime (the zone arrives inside the yearly prayer file), hence this parser.
// athan.cpp converts TzInfo into esphome::time::ParsedTimezone and calls time::set_global_tz().
//
// Offsets follow POSIX: seconds WEST of UTC (PST = +28800, Cairo EET = -7200).

#include <cstdint>

namespace esphome {
namespace athan {

enum class TzRuleType : uint8_t { NONE = 0, MONTH_WEEK_DAY = 1, JULIAN_NO_LEAP = 2, DAY_OF_YEAR = 3 };

struct TzRule {
  TzRuleType type{TzRuleType::NONE};
  uint8_t month{0};        // MONTH_WEEK_DAY: 1..12
  uint8_t week{0};         // MONTH_WEEK_DAY: 1..5, 5 = last
  uint8_t day_of_week{0};  // MONTH_WEEK_DAY: 0 = Sunday
  uint16_t day{0};         // JULIAN_NO_LEAP: 1..365 (29 Feb never counted); DAY_OF_YEAR: 0..365
  int32_t time_seconds{7200};  // local time of the switch, may be negative or above 24 h
};

struct TzInfo {
  int32_t std_offset{0};  // seconds west of UTC
  int32_t dst_offset{0};  // seconds west of UTC while DST is in force
  bool has_dst{false};
  TzRule start;  // switch to DST (given in standard local time)
  TzRule end;    // switch back (given in DST local time)
};

/// Parse a POSIX TZ string. Supports alphabetic and <quoted> names, hh[:mm[:ss]] offsets, M/J/n rules with
/// optional /time, and a DST name without rules (US rules assumed, as glibc does). Returns false on bad input.
bool parse_posix_tz(const char *text, TzInfo *out);

bool is_leap_year(int year);
int days_in_month(int year, int month);
/// Days since 1970-01-01 for a civil date (proleptic Gregorian).
int64_t days_from_civil(int year, int month, int day);
/// 1-based day of the year.
int day_of_year(int year, int month, int day);
/// Inverse of day_of_year.
void month_day_from_doy(int year, int doy, int *month, int *day);
/// 0 = Sunday.
int weekday(int year, int month, int day);

/// Seconds west of UTC in force at the UTC epoch `utc`.
int32_t tz_offset_at_utc(const TzInfo &tz, int64_t utc);
/// UTC epoch of a local wall-clock time (minutes after local midnight; may exceed 1439).
int64_t tz_local_to_utc(const TzInfo &tz, int year, int month, int day, int minutes);

}  // namespace athan
}  // namespace esphome
