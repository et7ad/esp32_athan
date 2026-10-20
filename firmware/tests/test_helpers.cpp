// Host unit tests for the pure helpers of the athan component (no ESP32 needed).
// Run: firmware/tests/run_tests.sh   (builds this with the computer's C++ compiler)
//
//   test_helpers <tz_vectors.txt> [file.mp3 expected_ms]...
//
// tz_vectors.txt lines: "<posix tz>|<utc epoch>|<expected seconds west>" (generated from Python's zoneinfo).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "../components/athan/day_window.h"
#include "../components/athan/mp3_check.h"
#include "../components/athan/tft_panel.h"
#include "../components/athan/tz_posix.h"

using namespace esphome::athan;

static int failures = 0;
#define CHECK(cond, ...)              \
  do {                                \
    if (!(cond)) {                    \
      failures++;                     \
      std::printf("FAIL: " __VA_ARGS__); \
      std::printf("\n");              \
    }                                 \
  } while (0)

static void test_calendar() {
  CHECK(days_from_civil(1970, 1, 1) == 0, "epoch day");
  CHECK(days_from_civil(2026, 10, 4) == 20730, "2026-10-04 = %lld", (long long) days_from_civil(2026, 10, 4));
  CHECK(weekday(2026, 10, 4) == 0, "2026-10-04 is a Sunday");
  CHECK(day_of_year(2024, 12, 31) == 366, "leap year length");
  CHECK(day_of_year(2026, 3, 1) == 60, "1 March in a common year");
  int m, d;
  month_day_from_doy(2024, 60, &m, &d);
  CHECK(m == 2 && d == 29, "doy 60 of 2024 = 29 Feb (got %d/%d)", m, d);
}

static void test_parse() {
  TzInfo tz;
  CHECK(parse_posix_tz("PST8PDT,M3.2.0,M11.1.0", &tz), "parse PST8PDT");
  CHECK(tz.std_offset == 28800 && tz.dst_offset == 25200 && tz.has_dst, "PST offsets");
  CHECK(tz.start.month == 3 && tz.start.week == 2 && tz.start.day_of_week == 0 && tz.start.time_seconds == 7200,
        "PST start rule");
  CHECK(parse_posix_tz("EET-2EEST,M4.5.5/0,M10.5.4/24", &tz), "parse Cairo");
  CHECK(tz.std_offset == -7200 && tz.dst_offset == -10800, "Cairo offsets");
  CHECK(tz.end.time_seconds == 86400, "Cairo end at 24:00");
  CHECK(parse_posix_tz("<+03>-3", &tz) && !tz.has_dst && tz.std_offset == -10800, "quoted name, no DST");
  CHECK(parse_posix_tz("IST-5:30", &tz) && tz.std_offset == -19800, "half-hour offset");
  CHECK(parse_posix_tz("EST5EDT", &tz) && tz.has_dst && tz.start.month == 3, "DST without rules -> US rules");
  CHECK(!parse_posix_tz("", &tz), "empty rejected");
  CHECK(!parse_posix_tz("PST8PDT,M3.2.0", &tz), "missing end rule rejected");
  CHECK(!parse_posix_tz("X8", &tz), "short name rejected");
  CHECK(!parse_posix_tz("PST8PDT,M13.2.0,M11.1.0", &tz), "bad month rejected");
}

static void test_vectors(const char *path) {
  std::ifstream f(path);
  if (!f) {
    std::printf("FAIL: cannot open %s\n", path);
    failures++;
    return;
  }
  std::string line;
  int n = 0, bad = 0;
  while (std::getline(f, line)) {
    size_t a = line.find('|'), b = line.rfind('|');
    if (a == std::string::npos || a == b)
      continue;
    std::string tzs = line.substr(0, a);
    long long t = std::atoll(line.substr(a + 1, b - a - 1).c_str());
    int expect = std::atoi(line.substr(b + 1).c_str());
    TzInfo tz;
    if (!parse_posix_tz(tzs.c_str(), &tz)) {
      bad++;
      continue;
    }
    n++;
    if (tz_offset_at_utc(tz, t) != expect) {
      if (bad < 5)
        std::printf("FAIL: %s at %lld: got %d expected %d\n", tzs.c_str(), t, tz_offset_at_utc(tz, t), expect);
      bad++;
    }
  }
  CHECK(bad == 0, "%d of %d time zone vectors wrong", bad, n);
  std::printf("time zone vectors: %d checked\n", n);

  // local -> UTC round trip on a DST-start day (02:30 does not exist; 10:00 does)
  TzInfo pst;
  parse_posix_tz("PST8PDT,M3.2.0,M11.1.0", &pst);
  long long t = tz_local_to_utc(pst, 2026, 3, 8, 10 * 60);
  CHECK(t == days_from_civil(2026, 3, 8) * 86400LL + 17 * 3600, "10:00 PDT on 8 Mar 2026 = 17:00 UTC");
  t = tz_local_to_utc(pst, 2026, 1, 15, 6 * 60);
  CHECK(t == days_from_civil(2026, 1, 15) * 86400LL + 14 * 3600, "06:00 PST = 14:00 UTC");
}

static void test_mp3(const char *path, long expected_ms) {
  std::ifstream f(path, std::ios::binary);
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  Mp3Info info = mp3_scan(data.data(), data.size());
  if (expected_ms < 0) {
    CHECK(!info.ok, "%s should be rejected", path);
    std::printf("mp3 %s: rejected (%s) as expected\n", path, info.error ? info.error : "?");
    return;
  }
  CHECK(info.ok, "%s: %s", path, info.error ? info.error : "");
  long diff = std::labs(static_cast<long>(info.duration_ms) - expected_ms);
  // 150 ms tolerance: ffprobe counts encoder delay/padding differently
  CHECK(diff <= 150, "%s: %u ms, expected %ld ms", path, info.duration_ms, expected_ms);
  std::printf("mp3 %s: %u ms (ffprobe %ld ms), %u Hz, %u ch, %u frames\n", path, info.duration_ms, expected_ms,
              info.sample_rate, info.channels, info.frames);
}

// The tick's and the night's windows: hours, prayers (today's times), across midnight, the menu's choices.
static void test_day_window() {
  const uint8_t h[7] = {5, 7, 7, 12, 16, 18, 19}, m[7] = {57, 8, 28, 58, 9, 40, 53};  // Fajr 05:57 ... Isha 19:53
  const int maghrib = WINDOW_PRAYER + 5, isha = WINDOW_PRAYER + 6, fajr = WINDOW_PRAYER, dhuhr = WINDOW_PRAYER + 3;
  CHECK(window_minute(isha, h, m) == 19 * 60 + 53, "Isha's minute");
  CHECK(in_window(22, 6, 23 * 60, h, m) && in_window(22, 6, 2 * 60, h, m) && !in_window(22, 6, 12 * 60, h, m), "22-6");
  CHECK(in_window(7, 7, 3 * 60, h, m), "from = until: all day");
  CHECK(in_window(isha, fajr, 20 * 60, h, m) && in_window(isha, fajr, 5 * 60 + 56, h, m), "Isha to Fajr: inside");
  CHECK(!in_window(isha, fajr, 19 * 60 + 52, h, m) && !in_window(isha, fajr, 5 * 60 + 57, h, m), "Isha to Fajr: ends");
  CHECK(in_window(maghrib, dhuhr, 9 * 60, h, m) && !in_window(maghrib, dhuhr, 13 * 60, h, m), "Maghrib to Dhuhr");
  CHECK(in_window(6, isha, 19 * 60, h, m) && !in_window(6, isha, 20 * 60, h, m), "06:00 to Isha (tick)");
  CHECK(window_length(isha, fajr, h, m) == 604, "Isha to Fajr is 10 h 4 min (got %d)", window_length(isha, fajr, h, m));
  const WindowKind TICK = WindowKind::TICK, NIGHT = WindowKind::NIGHT;
  for (WindowKind k : {TICK, NIGHT})
    for (int until = 0; until < 2; until++)
      for (int i = 0; i < WINDOW_CHOICES; i++)
        CHECK(window_choice(k, until, window_value(k, until, i)) == i, "choice %d round trip (until %d)", i, until);
  // The night runs from dusk to dawn, the tick's day window the other way round.
  CHECK(window_value(NIGHT, false, 0) == maghrib && window_value(NIGHT, false, 1) == isha, "night from: Maghrib, Isha");
  CHECK(window_value(NIGHT, true, 0) == fajr && window_value(NIGHT, true, 1) == dhuhr, "night until: Fajr, Dhuhr");
  CHECK(window_value(TICK, false, 0) == fajr && window_value(TICK, false, 1) == dhuhr, "tick from: Fajr, Dhuhr");
  CHECK(window_value(TICK, true, 0) == maghrib && window_value(TICK, true, 1) == isha, "tick until: Maghrib, Isha");
  CHECK(window_choice(TICK, false, 22) == 24 && window_value(TICK, false, 24) == 22, "hours keep their meaning");
  CHECK(window_choice(TICK, false, isha) < 0 && window_choice(TICK, true, dhuhr) < 0, "the tick does not offer these");
  // A tick window saved with ends it no longer offers ("Isha to Dhuhr") becomes the hours with the same ticks.
  CHECK(window_sanitize(TICK, false, isha, h, m) == 20, "tick from Isha (19:53): 20:00, its first tick (got %d)",
        window_sanitize(TICK, false, isha, h, m));
  CHECK(window_sanitize(TICK, true, dhuhr, h, m) == 13, "tick until Dhuhr (12:58): 13:00, 12:00 still ticks");
  CHECK(window_sanitize(TICK, false, fajr, h, m) == fajr && window_sanitize(NIGHT, false, isha, h, m) == isha,
        "offered ends stay");
  CHECK(window_sanitize(TICK, true, 7, h, m) == 7 && window_sanitize(NIGHT, true, 42, h, m) == 0, "hours stay, junk: 0");
  const uint8_t late[7] = {5, 7, 7, 12, 16, 18, 23}, latem[7] = {57, 8, 28, 58, 9, 40, 30};  // Isha at 23:30
  CHECK(window_sanitize(TICK, false, isha, late, latem) == 0, "23:30 rounds up to 00:00");
  // Screen at Night: Normal is the day's level at every brightness (0 % = dark by day too), Dim the lower of
  // Brightness and the dim level, Turned off dark.
  for (int bright = 0; bright <= 100; bright++) {
    const int dim = bright > 0 ? (bright < 10 ? bright : 10) : 10;  // screen_refresh's dim level (screen_dim_pct 10)
    CHECK(night_level(NIGHT_NORMAL, bright, dim) == bright, "Normal at %d %% is the day's level", bright);
    CHECK(night_level(NIGHT_DIM, bright, dim) == (bright < dim ? bright : dim), "Dim at %d %%", bright);
    CHECK(night_level(NIGHT_OFF, bright, dim) == 0, "Turned off at %d %%", bright);
  }
  CHECK(night_level(7, 60, 10) == 60, "an unknown setting behaves as Normal");
}

// The TFT's start-up commands sent again (tft_panel.h), split around Sleep Out with the pauses left out. The sequence
// is ESPHome 2026.9.1's for this ST7789V, as the generated main.cpp gives it (tft->set_init_sequence).
static std::string panel_cmds(const std::vector<uint8_t> &seq, tft::PanelPart part, bool *ok = nullptr) {
  std::string out;
  const bool good = tft::panel_part(seq, part, [&out](uint8_t cmd, const uint8_t *args, size_t n) {
    char b[8];
    std::snprintf(b, sizeof(b), "%s%02X:", out.empty() ? "" : " ", cmd);
    out += b;
    for (size_t i = 0; i < n; i++) {
      std::snprintf(b, sizeof(b), "%02X", args[i]);
      out += b;
    }
  });
  if (ok != nullptr)
    *ok = good;
  return out;
}

static void test_panel_sequence() {
  using tft::PanelPart;
  const std::vector<uint8_t> st7789v = {
      10,  255, 182, 2,  10, 130, 178, 5,   12,  12, 0,   51, 51,  183, 1,   53, 187, 1,  40,  192, 1,  12,
      194, 2,   1,   255, 195, 1, 16,  196, 1,   32, 198, 1,  15,  208, 2,   164, 161, 224, 14, 208, 0,  2,
      7,   10,  40,  50, 68, 66,  6,   14,  18,  20, 23,  225, 14,  208, 0,   2,  7,   10, 40,  49, 84, 71,
      14,  28,  23,  27, 30, 58,  1,   85,  33,  0,  0,   255, 17,  0,   10,  255, 41,  0,  10,  255};
  bool ok = false;
  const std::string settings = panel_cmds(st7789v, PanelPart::SETTINGS, &ok);
  CHECK(ok, "ST7789V sequence read");
  CHECK(settings == "B6:0A82 B2:0C0C003333 B7:35 BB:28 C0:0C C2:01FF C3:10 C4:20 C6:0F D0:A4A1 "
                    "E0:D00002070A28324442060E121417 E1:D00002070A283154470E1C171B1E 3A:55 21:",
        "settings: power, gamma, colour format, inversion (got %s)", settings.c_str());
  CHECK(panel_cmds(st7789v, PanelPart::SLEEP_OUT) == "11:", "Sleep Out alone");
  CHECK(panel_cmds(st7789v, PanelPart::AFTER_WAKE) == "29:", "then Display On");
  // A pause of 17 ms (0x11) is not a Sleep Out; without one, everything is a setting.
  const std::vector<uint8_t> no_wake = {17, 255, 0x29, 0};
  CHECK(panel_cmds(no_wake, PanelPart::SETTINGS) == "29:" && panel_cmds(no_wake, PanelPart::SLEEP_OUT).empty() &&
            panel_cmds(no_wake, PanelPart::AFTER_WAKE).empty(),
        "no Sleep Out: all settings");
  // n's top bit is a flag; a second Sleep Out belongs after the first.
  const std::vector<uint8_t> flags = {0x36, 0x81, 0x08, 0x11, 0, 0x11, 0, 0x29, 0};
  CHECK(panel_cmds(flags, PanelPart::SETTINGS) == "36:08" && panel_cmds(flags, PanelPart::AFTER_WAKE) == "11: 29:",
        "flag bit, second Sleep Out");
  panel_cmds({0xB6, 2, 0x0A}, PanelPart::SETTINGS, &ok);
  CHECK(!ok, "arguments cut off: malformed");
  panel_cmds({0x29, 0, 0xB6}, PanelPart::SETTINGS, &ok);
  CHECK(!ok, "command without its length: malformed");
  CHECK(panel_cmds({}, PanelPart::SETTINGS, &ok).empty() && ok, "empty sequence: nothing to send");
}

int main(int argc, char **argv) {
  test_calendar();
  test_day_window();
  test_panel_sequence();
  test_parse();
  if (argc > 1)
    test_vectors(argv[1]);
  for (int i = 2; i + 1 < argc; i += 2)
    test_mp3(argv[i], std::atol(argv[i + 1]));
  uint8_t junk[4096];
  for (size_t i = 0; i < sizeof(junk); i++)
    junk[i] = static_cast<uint8_t>(i * 37 + 11);
  CHECK(!mp3_scan(junk, sizeof(junk)).ok, "random bytes rejected");
  std::printf(failures ? "\n%d FAILURE(S)\n" : "\nall tests passed\n", failures);
  return failures ? 1 : 0;
}
