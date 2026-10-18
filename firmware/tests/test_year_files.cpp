// The clock's own checks on the computer: parse_year_file() (prayer_store.cpp) on yearly files, and
// parse_posix_tz() (tz_posix.cpp) on time zone strings. Built by run_tests.sh when ArduinoJson is on this computer
// (stand-ins for ESPHome and ESP-IDF headers in tests/host_prayer/); check_docs.py runs it.
//
//   test_year_files docs/athantimes/<key>/<year>.json ...   one line per file: "OK <key>/<year>" or "BAD ...: why"
//   test_year_files tz:<posix tz> ...                        one line per string: "accepted" or "REFUSED" (last)
//   test_year_files mosques:docs/athantimes/mosques.json     "mosques offered: <key> ...", "... shown: <name>|...", "... at: <lat>,<lon>|..."
//   test_year_files mosques-selftest                         parse_mosques() on made-up files: "mosques self-test ..."
//
// Exit code 1 when a file is refused. The key and year come from the path, as the clock asks for them.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "../components/athan/prayer_store.h"
#include "../components/athan/tz_posix.h"

extern "C" {
uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t *buf, uint32_t len) {
  crc = ~crc;
  for (uint32_t i = 0; i < len; i++) {
    crc ^= buf[i];
    for (int k = 0; k < 8; k++)
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}
const esp_partition_t *esp_partition_find_first(esp_partition_type_t, esp_partition_subtype_t, const char *) {
  return nullptr;
}
esp_err_t esp_partition_read(const esp_partition_t *, size_t, void *, size_t) { return 1; }
esp_err_t esp_partition_write(const esp_partition_t *, size_t, const void *, size_t) { return 1; }
esp_err_t esp_partition_erase_range(const esp_partition_t *, size_t, size_t) { return 1; }
}

using namespace esphome::athan;

// What parse_mosques() offers: the keys, the labels shown and where each is ("|" between them; "-" for unknown).
static std::string offered(const std::string &json, bool *ok, std::string *err, std::string *labels = nullptr,
                           std::string *places = nullptr) {
  PsramVector<MosqueEntry> list;
  *ok = parse_mosques(reinterpret_cast<const uint8_t *>(json.data()), json.size(), &list, err);
  std::string keys, shown, at;
  for (const auto &m : list) {
    keys += (keys.empty() ? "" : " ") + std::string(m.key.data(), m.key.size());
    shown += (shown.empty() ? "" : "|") + std::string(m.label.data(), m.label.size());
    char p[48] = "-";
    if (!std::isnan(m.lat) && !std::isnan(m.lon))
      std::snprintf(p, sizeof(p), "%.6f,%.6f", m.lat, m.lon);
    at += (at.empty() ? "" : "|") + std::string(p);
  }
  if (labels != nullptr)
    *labels = shown;
  if (places != nullptr)
    *places = at;
  return keys;
}

// parse_mosques() on made-up files: what is offered, in what order, shown how, and when the clock keeps its old list.
static int mosques_selftest() {
  struct Case {
    const char *json;
    bool ok;
    const char *keys;
    const char *labels;  // nullptr: not checked
    const char *places = nullptr;  // nullptr: not checked
  };
  const std::string long_name(41, 'x');
  const std::string cases_long = R"({"mosques": {"a": {"name": ")" + long_name + R"("}, "b": {"name": ")" +
                                 long_name.substr(1) + R"("}}})";
  const Case cases[] = {
      {R"({"mosques": {"b": {"name": "Bee"}, "a": {}, "c_2": {"active": true, "name": "C two"}}})", true, "b a c_2",
       "Bee|a|C two"},  // file order; no name: the key
      {R"({"mosques": {"a": {"active": false}, "b": {}}})", true, "b", "b"},
      {R"({"mosques": {"a": {"active": "no"}}})", true, "a", "a"},  // only a JSON false takes one out
      {R"({"mosques": {"Davis": {}, "da vis": {}, "": {}, "a-b": {}, "ok": {}}})", true, "ok", "ok"},
      {R"({"mosques": {"abcdefghijklmnopqrstuvwxyz01234": {}, "abcdefghijklmnopqrstuvwxyz012345": {}}})", true,
       "abcdefghijklmnopqrstuvwxyz01234", nullptr},  // keys of 31 characters at most
      {R"({"mosques": {"w": {"name": "Masjid é"}, "x": {"name": "   "}, "y": {"name": 5}, "z": {"name": "Tab\there"}}})",
       true, "w x y z", "w|x|y|z"},  // a name the fonts cannot show, or not a name: the key
      {cases_long.c_str(), true, "a b", "a|xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"},  // 40 characters at most
      {R"({"mosques": {"a": {"name": "Same"}, "b": {"name": "Same"}}})", true, "a b", "Same|Same"},  // numbers part them
      {R"({"mosques": {"a": {"name": "b"}, "b": {}}})", true, "a b", "b|b"},  // b has no name: its key
      {R"({"mosques": {"a": {"name": "A1"}, "a": {"name": "A2"}}})", true, "a", nullptr},  // a key twice: once
      {R"({"mosques": {"a": {"lat": 38.546506, "lon": -121.756307}, "b": {"lat": 38, "lon": -121}}})", true, "a b",
       nullptr, "38.546506,-121.756307|38.000000,-121.000000"},  // as a map gives them; whole numbers too
      {R"({"mosques": {"a": {"lat": 91, "lon": 0}, "b": {"lat": 0, "lon": -180.5}, "c": {"lat": "38.5", "lon": 1},
          "d": {"lat": 38.5}, "e": {}}})",
       true, "a b c d e", nullptr, "-|-|-|-|-"},  // out of range, not numbers, half given, none: unknown
      {R"({"mosques": {"a": {"active": false}}})", false, "", nullptr},  // nothing offered: keep the old list
      {R"({"mosques": {}})", false, "", nullptr},
      {R"({"mosques": ["a", "b"]})", false, "", nullptr},
      {R"({"v": 1})", false, "", nullptr},
      {R"([1, 2])", false, "", nullptr},
      {R"({"mosques": {"a": {})", false, "", nullptr},  // cut short
      {"", false, "", nullptr},
  };
  int failed = 0;
  for (const auto &c : cases) {
    bool ok;
    std::string err, labels, places;
    const std::string got = offered(c.json, &ok, &err, &labels, &places);
    if (ok != c.ok || got != c.keys || (c.labels != nullptr && labels != c.labels) ||
        (c.places != nullptr && places != c.places)) {
      std::printf("mosques self-test FAILED on %s: got %s \"%s\" shown \"%s\" at \"%s\"\n", c.json,
                  ok ? "ok" : "refused", got.c_str(), labels.c_str(), places.c_str());
      failed++;
    }
  }
  std::printf("mosques self-test: %d of %zu cases passed\n", (int) (sizeof(cases) / sizeof(cases[0])) - failed,
              sizeof(cases) / sizeof(cases[0]));
  return failed;
}

int main(int argc, char **argv) {
  int refused = 0;
  for (int i = 1; i < argc; i++) {
    const std::string arg = argv[i];
    if (arg == "mosques-selftest") {
      refused += mosques_selftest();
      continue;
    }
    if (arg.rfind("mosques:", 0) == 0) {
      std::ifstream f(arg.substr(8), std::ios::binary);
      std::stringstream ss;
      ss << f.rdbuf();
      const std::string data = ss.str();
      bool ok = false;
      std::string err = "cannot read the file", keys, labels, places;
      if (f && data.size() > MAX_MOSQUES_FILE_BYTES)
        err = "file over 32 KB";  // what job_mosques_() reports for a download over the limit
      else if (f)
        keys = offered(data, &ok, &err, &labels, &places);
      if (ok) {
        std::printf("mosques offered: %s\nmosques shown: %s\nmosques at: %s\n", keys.c_str(), labels.c_str(),
                    places.c_str());
      } else {
        std::printf("BAD mosques: %s\n", err.c_str());
        refused++;
      }
      continue;
    }
    if (arg.rfind("tz:", 0) == 0) {
      TzInfo tz;
      std::printf("tz %s %s\n", arg.c_str() + 3, parse_posix_tz(arg.c_str() + 3, &tz) ? "accepted" : "REFUSED");
      continue;
    }
    // .../<key>/<year>.json
    const size_t slash = arg.find_last_of('/');
    const size_t slash2 = slash == std::string::npos || slash == 0 ? std::string::npos : arg.find_last_of('/', slash - 1);
    if (slash2 == std::string::npos || arg.size() < slash + 6) {
      std::printf("BAD %s: not a <key>/<year>.json path\n", arg.c_str());
      refused++;
      continue;
    }
    const std::string key = arg.substr(slash2 + 1, slash - slash2 - 1);
    const int year = std::atoi(arg.substr(slash + 1, 4).c_str());
    std::ifstream f(arg, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string data = ss.str();
    PsramVector<uint16_t> table;
    std::string tz, err;
    bool ok = false;
    if (!f)
      err = "cannot read the file";
    else if (data.size() > MAX_YEAR_FILE_BYTES)
      err = "file over 100 KB";  // what job_prayer_() reports for a download over the limit
    else
      ok = parse_year_file(reinterpret_cast<const uint8_t *>(data.data()), data.size(), key, year, &table, &tz, &err);
    if (ok && (key.size() >= 32 || tz.size() >= 64)) {  // PrayerStore::write() refuses these
      ok = false;
      err = "key or time zone too long to store";
    }
    if (ok) {
      std::printf("OK %s/%d (%zu days)\n", key.c_str(), year, table.size() / NUM_FIELDS);
    } else {
      std::printf("BAD %s/%d: %s\n", key.c_str(), year, err.c_str());
      refused++;
    }
  }
  return refused ? 1 : 0;
}
