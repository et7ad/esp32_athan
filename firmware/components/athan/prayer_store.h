#pragma once
// Stored prayer-time years in the "prayer" flash partition (128 KB, see firmware/partitions.csv).
//
// 8 slots of 16 KB. A slot = header (key, year, time zone, CRCs) in its first 4 KB sector, then the table:
// one uint16 per time (minutes after midnight), 12 per day in the order of FIELD_NAMES, 365 or 366 days
// (8.8 KB; in RAM it is a PsramVector). A new year is written into a free or least useful slot, header LAST, so a
// power cut while writing never damages a stored year. Duplicates resolve to the highest write counter.
//
// The yearly file it comes from: docs/athantimes/<key>/<year>.json, format in prayertimes_specs.md.

#include <cmath>
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

static const size_t MAX_MOSQUES_FILE_BYTES = 32 * 1024;
static const size_t MAX_MOSQUE_NAME = 40;
/// A mosque key as the clock stores it (selected_location_key, a stored year): 1 to 31 of a-z, 0-9 and _, the rule of
/// scripts/athan_yearly.py.
bool valid_mosque_key(const char *key);
/// A mosque name the clock can show: 1 to MAX_MOSQUE_NAME printable ASCII characters (the menu fonts have no others),
/// not only spaces. A longer one would still fit the menu cut short; this only bounds it.
bool valid_mosque_name(const char *name);
/// One mosque the clocks offer: its key, which the clock stores and downloads the prayer times by (owners never see
/// it), its label, which the menu and the web page show, and where it is (NAN when the file does not say).
struct MosqueEntry {
  PsramString key;
  PsramString label;
  double lat{NAN};
  double lon{NAN};
  bool operator==(const MosqueEntry &o) const {
    auto same = [](double a, double b) { return a == b || (std::isnan(a) && std::isnan(b)); };
    return this->key == o.key && this->label == o.label && same(this->lat, o.lat) && same(this->lon, o.lon);
  }
  bool operator!=(const MosqueEntry &o) const { return !(*this == o); }
};
/// The mosques the clocks offer, from docs/athantimes/mosques.json: the entries of its "mosques" object in file order,
/// without those marked "active": false, a key valid_mosque_key() refuses, or a key seen before. The label is the
/// entry's "name" when valid_mosque_name() takes it, else the key (data check_docs.py refuses). Equal names may stay:
/// the choices number every label (location_choices()), which keeps them apart. "lat" and "lon" (as a map gives them,
/// decimal degrees) are kept when both are numbers in range. False (with err) when the file is not that shape or
/// offers no mosque: the clock then keeps the list it had.
bool parse_mosques(const uint8_t *data, size_t len, PsramVector<MosqueEntry> *out, std::string *err);

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
