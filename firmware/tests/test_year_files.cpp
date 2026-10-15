// The clock's own checks on the computer: parse_year_file() (prayer_store.cpp) on yearly files, and
// parse_posix_tz() (tz_posix.cpp) on time zone strings. Built by run_tests.sh when ArduinoJson is on this computer
// (stand-ins for ESPHome and ESP-IDF headers in tests/host_prayer/); check_docs.py runs it.
//
//   test_year_files docs/athantimes/<key>/<year>.json ...   one line per file: "OK <key>/<year>" or "BAD ...: why"
//   test_year_files tz:<posix tz> ...                        one line per string: "accepted" or "REFUSED" (last)
//
// Exit code 1 when a file is refused. The key and year come from the path, as the clock asks for them.

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

int main(int argc, char **argv) {
  int refused = 0;
  for (int i = 1; i < argc; i++) {
    const std::string arg = argv[i];
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
