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

#include "../components/athan/mp3_check.h"
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

int main(int argc, char **argv) {
  test_calendar();
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
