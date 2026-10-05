#include "tz_posix.h"

#include <cctype>
#include <cstring>

namespace esphome {
namespace athan {

namespace {

// Zone name: 3+ letters, or anything between < and >.
bool parse_name(const char *&p) {
  if (*p == '<') {
    const char *q = std::strchr(p, '>');
    if (q == nullptr || q - p < 2)
      return false;
    p = q + 1;
    return true;
  }
  const char *start = p;
  while (std::isalpha(static_cast<unsigned char>(*p)))
    p++;
  return p - start >= 3;
}

// [+|-]hh[:mm[:ss]] -> seconds (sign kept as written).
bool parse_hms(const char *&p, int32_t *out) {
  int sign = 1;
  if (*p == '+' || *p == '-') {
    if (*p == '-')
      sign = -1;
    p++;
  }
  if (!std::isdigit(static_cast<unsigned char>(*p)))
    return false;
  int32_t parts[3] = {0, 0, 0};
  for (int i = 0; i < 3; i++) {
    int digits = 0;
    int32_t v = 0;
    while (std::isdigit(static_cast<unsigned char>(*p)) && digits < 3) {
      v = v * 10 + (*p - '0');
      p++;
      digits++;
    }
    if (digits == 0)
      return false;
    parts[i] = v;
    if (i < 2 && *p == ':' && std::isdigit(static_cast<unsigned char>(p[1]))) {
      p++;
      continue;
    }
    break;
  }
  *out = sign * (parts[0] * 3600 + parts[1] * 60 + parts[2]);
  return true;
}

bool parse_uint(const char *&p, int *out) {
  if (!std::isdigit(static_cast<unsigned char>(*p)))
    return false;
  int v = 0;
  while (std::isdigit(static_cast<unsigned char>(*p))) {
    v = v * 10 + (*p - '0');
    p++;
  }
  *out = v;
  return true;
}

bool parse_rule(const char *&p, TzRule *r) {
  int a = 0, b = 0, c = 0;
  if (*p == 'M') {
    p++;
    if (!parse_uint(p, &a) || *p != '.')
      return false;
    p++;
    if (!parse_uint(p, &b) || *p != '.')
      return false;
    p++;
    if (!parse_uint(p, &c))
      return false;
    if (a < 1 || a > 12 || b < 1 || b > 5 || c > 6)
      return false;
    r->type = TzRuleType::MONTH_WEEK_DAY;
    r->month = a;
    r->week = b;
    r->day_of_week = c;
  } else if (*p == 'J') {
    p++;
    if (!parse_uint(p, &a) || a < 1 || a > 365)
      return false;
    r->type = TzRuleType::JULIAN_NO_LEAP;
    r->day = a;
  } else {
    if (!parse_uint(p, &a) || a > 365)
      return false;
    r->type = TzRuleType::DAY_OF_YEAR;
    r->day = a;
  }
  r->time_seconds = 7200;
  if (*p == '/') {
    p++;
    int32_t t;
    if (!parse_hms(p, &t))
      return false;
    r->time_seconds = t;
  }
  return true;
}

// Local date of a rule in `year`, as days since the epoch.
int64_t rule_day(const TzRule &r, int year) {
  switch (r.type) {
    case TzRuleType::MONTH_WEEK_DAY: {
      int first_wday = weekday(year, r.month, 1);
      int day = 1 + ((r.day_of_week - first_wday) % 7 + 7) % 7 + (r.week - 1) * 7;
      int dim = days_in_month(year, r.month);
      while (day > dim)
        day -= 7;
      return days_from_civil(year, r.month, day);
    }
    case TzRuleType::JULIAN_NO_LEAP: {
      int m, d;
      month_day_from_doy(2001, r.day, &m, &d);  // 2001 is not a leap year: 29 Feb never counted
      return days_from_civil(year, m, d);
    }
    case TzRuleType::DAY_OF_YEAR:
      return days_from_civil(year, 1, 1) + r.day;
    default:
      return 0;
  }
}

int year_of_epoch(int64_t t) {
  int64_t days = t / 86400;
  if (t % 86400 < 0)
    days--;
  // civil_from_days (Howard Hinnant)
  days += 719468;
  int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  int64_t doe = days - era * 146097;
  int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t y = yoe + era * 400;
  int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  int64_t mp = (5 * doy + 2) / 153;
  int64_t m = mp + (mp < 10 ? 3 : -9);
  return static_cast<int>(y + (m <= 2 ? 1 : 0));
}

}  // namespace

bool is_leap_year(int year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

int days_in_month(int year, int month) {
  static const int DAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && is_leap_year(year))
    return 29;
  return DAYS[(month - 1) % 12];
}

int64_t days_from_civil(int year, int month, int day) {
  int64_t y = year - (month <= 2 ? 1 : 0);
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  int64_t yoe = y - era * 400;
  int64_t mp = month + (month > 2 ? -3 : 9);
  int64_t doy = (153 * mp + 2) / 5 + day - 1;
  int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

int day_of_year(int year, int month, int day) {
  return static_cast<int>(days_from_civil(year, month, day) - days_from_civil(year, 1, 1)) + 1;
}

void month_day_from_doy(int year, int doy, int *month, int *day) {
  int m = 1;
  while (m < 12 && doy > days_in_month(year, m)) {
    doy -= days_in_month(year, m);
    m++;
  }
  *month = m;
  *day = doy;
}

int weekday(int year, int month, int day) {
  int64_t d = days_from_civil(year, month, day);  // 1970-01-01 was a Thursday (4)
  return static_cast<int>(((d % 7) + 7 + 4) % 7);
}

bool parse_posix_tz(const char *text, TzInfo *out) {
  if (text == nullptr || out == nullptr)
    return false;
  TzInfo tz;
  const char *p = text;
  while (*p == ' ')
    p++;
  if (!parse_name(p) || !parse_hms(p, &tz.std_offset))
    return false;
  tz.dst_offset = tz.std_offset;
  if (*p != '\0' && *p != ',') {
    if (!parse_name(p))
      return false;
    tz.has_dst = true;
    tz.dst_offset = tz.std_offset - 3600;
    if (*p != '\0' && *p != ',') {
      if (!parse_hms(p, &tz.dst_offset))
        return false;
    }
    if (*p == ',') {
      p++;
      if (!parse_rule(p, &tz.start) || *p != ',')
        return false;
      p++;
      if (!parse_rule(p, &tz.end))
        return false;
    } else {
      // No rule: US rules since 2007, the glibc default.
      tz.start = {TzRuleType::MONTH_WEEK_DAY, 3, 2, 0, 0, 7200};
      tz.end = {TzRuleType::MONTH_WEEK_DAY, 11, 1, 0, 0, 7200};
    }
  }
  while (*p == ' ')
    p++;
  if (*p != '\0')
    return false;
  *out = tz;
  return true;
}

int32_t tz_offset_at_utc(const TzInfo &tz, int64_t utc) {
  if (!tz.has_dst)
    return tz.std_offset;
  int year = year_of_epoch(utc - tz.std_offset);
  int64_t start = rule_day(tz.start, year) * 86400 + tz.start.time_seconds + tz.std_offset;
  int64_t end = rule_day(tz.end, year) * 86400 + tz.end.time_seconds + tz.dst_offset;
  bool in_dst = (start < end) ? (utc >= start && utc < end) : !(utc >= end && utc < start);
  return in_dst ? tz.dst_offset : tz.std_offset;
}

int64_t tz_local_to_utc(const TzInfo &tz, int year, int month, int day, int minutes) {
  int64_t local = days_from_civil(year, month, day) * 86400 + static_cast<int64_t>(minutes) * 60;
  int64_t t = local + tz.std_offset;
  if (tz_offset_at_utc(tz, t) != tz.std_offset)
    t = local + tz.dst_offset;
  return t;
}

}  // namespace athan
}  // namespace esphome
