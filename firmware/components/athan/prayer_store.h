#pragma once
// Stored prayer-time years in the "prayer" flash partition (128 KB, see firmware/partitions.csv).
//
// 8 slots of 16 KB. A slot = header (key, year, time zone, CRCs) in its first 4 KB sector, then the table:
// one uint16 per time (minutes after midnight), 12 per day in the order of FIELD_NAMES, 365 or 366 days
// (8.8 KB; in RAM it is a PsramVector). A new year is written into a free or least useful slot, header LAST, so a
// power cut while writing never damages a stored year. Duplicates resolve to the highest write counter.
//
// The yearly file it comes from: docs/athantimes/<key>/<year>.json, format in prayertimes_specs.md.

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <esp_partition.h>

#include "psram_alloc.h"

namespace esphome {
namespace athan {

static const int NUM_FIELDS = 12;
static const char *const FIELD_NAMES[NUM_FIELDS] = {"fajr",       "fajr_iqa", "sunrise", "doha",
                                                    "dhuhar",     "dhuhar_iqa", "asr", "asr_iqa",
                                                    "maghrib",    "maghrib_iqa", "isha", "isha_iqa"};
// Column of each of the 7 schedule entries used by the firmware (Fajr, Sunrise, Doha, Dhuhr, Asr, Maghrib, Isha).
static const int ADHAN_COLUMN[7] = {0, 2, 3, 4, 6, 8, 10};
static const size_t MAX_YEAR_FILE_BYTES = 100000;

/// Parse and check a yearly file. On success fills table (days * 12 values) and tz. On failure fills err.
bool parse_year_file(const uint8_t *data, size_t len, const std::string &key, int year, PsramVector<uint16_t> *table,
                     std::string *tz, std::string *err);

class PrayerStore {
 public:
  bool begin();
  bool ready() const { return this->part_ != nullptr; }
  /// Slot holding (key, year), or -1.
  int find(const std::string &key, int year) const;
  bool has(const std::string &key, int year) const { return this->find(key, year) >= 0; }
  /// Read a stored year (main loop).
  bool load(int slot, PsramVector<uint16_t> *table, std::string *tz) const;
  /// Store a year (worker task). Never overwrites a slot listed in `keep` unless it holds the same year.
  bool write(const std::string &key, int year, const std::string &tz, const PsramVector<uint16_t> &table,
             const std::vector<std::pair<std::string, int>> &keep);
  /// Whether any year of a key is stored.
  bool has_any(const std::string &key) const;
  /// "2026, 2027" for a key.
  std::string years_of(const std::string &key) const;
  /// Time zone of any stored year of `key` (the latest year), or "".
  std::string any_tz(const std::string &key) const;

 protected:
  struct Header {
    uint32_t magic;
    uint32_t version;
    uint32_t seq;
    char key[32];
    uint16_t year;
    uint16_t days;
    char tz[64];
    uint32_t table_crc;
    uint32_t header_crc;
  };
  bool read_header_(int slot, Header *h) const;

  const esp_partition_t *part_{nullptr};
  Header header_[8]{};
  bool valid_[8]{};
  uint32_t next_seq_{1};
  mutable std::mutex mutex_;
};

}  // namespace athan
}  // namespace esphome
