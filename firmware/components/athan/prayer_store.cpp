#include "prayer_store.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

#include "esphome/components/json/json_util.h"
#include "esphome/core/log.h"

#include <esp_rom_crc.h>

#include "tz_posix.h"

namespace esphome {
namespace athan {

static const char *const TAG = "athan.prayer";

static const int SLOTS = 8;
static const uint32_t SLOT_SIZE = 16384;
static const uint32_t TABLE_OFFSET = 4096;
static const uint32_t MAGIC = 0x31595450;  // "PTY1"
static const uint32_t VERSION = 1;
static const esp_partition_subtype_t PRAYER_SUBTYPE = static_cast<esp_partition_subtype_t>(0x41);

static bool parse_hhmm(const char *s, uint16_t *out) {
  if (s == nullptr || std::strlen(s) != 5 || s[2] != ':')
    return false;
  for (int i : {0, 1, 3, 4})
    if (s[i] < '0' || s[i] > '9')
      return false;
  int h = (s[0] - '0') * 10 + (s[1] - '0');
  int m = (s[3] - '0') * 10 + (s[4] - '0');
  if (h > 23 || m > 59)
    return false;
  *out = static_cast<uint16_t>(h * 60 + m);
  return true;
}

bool parse_year_file(const uint8_t *data, size_t len, const std::string &key, int year, std::vector<uint16_t> *table,
                     std::string *tz, std::string *err) {
  JsonDocument doc = json::parse_json(data, len);
  JsonObject root = doc.as<JsonObject>();
  if (root.isNull()) {
    *err = "not valid JSON";
    return false;
  }
  if (!root["location"].is<const char *>() || key != root["location"].as<const char *>()) {
    *err = "file is for another mosque";
    return false;
  }
  if (!root["year"].is<int>() || root["year"].as<int>() != year) {
    *err = "file is for another year";
    return false;
  }
  if (!root["tz"].is<const char *>()) {
    *err = "no time zone";
    return false;
  }
  TzInfo tzi;
  if (!parse_posix_tz(root["tz"].as<const char *>(), &tzi)) {
    *err = "bad time zone";
    return false;
  }
  JsonArray fields = root["fields"].as<JsonArray>();
  int column_of[NUM_FIELDS];
  for (int f = 0; f < NUM_FIELDS; f++) {
    column_of[f] = -1;
    int i = 0;
    for (JsonVariant v : fields) {
      if (v.is<const char *>() && std::strcmp(v.as<const char *>(), FIELD_NAMES[f]) == 0)
        column_of[f] = i;
      i++;
    }
    if (column_of[f] < 0) {
      *err = std::string("field missing: ") + FIELD_NAMES[f];
      return false;
    }
  }
  JsonArray days = root["days"].as<JsonArray>();
  const int expected = is_leap_year(year) ? 366 : 365;
  if (days.isNull() || static_cast<int>(days.size()) != expected) {
    *err = "wrong number of days";
    return false;
  }
  table->assign(static_cast<size_t>(expected) * NUM_FIELDS, 0);
  int d = 0;
  for (JsonVariant day : days) {
    JsonArray row = day.as<JsonArray>();
    if (row.isNull()) {
      *err = "a day is not a list";
      return false;
    }
    for (int f = 0; f < NUM_FIELDS; f++) {
      uint16_t v;
      if (!parse_hhmm(row[column_of[f]].as<const char *>(), &v)) {
        *err = "bad time on day " + std::to_string(d + 1);
        return false;
      }
      (*table)[d * NUM_FIELDS + f] = v;
    }
    for (int i = 1; i < 7; i++) {
      if ((*table)[d * NUM_FIELDS + ADHAN_COLUMN[i]] <= (*table)[d * NUM_FIELDS + ADHAN_COLUMN[i - 1]]) {
        *err = "times out of order on day " + std::to_string(d + 1);
        return false;
      }
    }
    d++;
  }
  *tz = root["tz"].as<const char *>();
  return true;
}

bool PrayerStore::begin() {
  this->part_ = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, PRAYER_SUBTYPE, "prayer");
  if (this->part_ == nullptr || this->part_->size < SLOTS * SLOT_SIZE) {
    ESP_LOGE(TAG, "No 'prayer' partition: flash the partition table from firmware/partitions.csv over USB once");
    this->part_ = nullptr;
    return false;
  }
  std::lock_guard<std::mutex> lock(this->mutex_);
  for (int s = 0; s < SLOTS; s++) {
    Header h;
    this->valid_[s] = this->read_header_(s, &h);
    if (!this->valid_[s])
      continue;
    // Verify the table too.
    std::vector<uint16_t> table(h.days * NUM_FIELDS);
    if (esp_partition_read(this->part_, s * SLOT_SIZE + TABLE_OFFSET, table.data(), table.size() * 2) != ESP_OK ||
        esp_rom_crc32_le(0, reinterpret_cast<const uint8_t *>(table.data()), table.size() * 2) != h.table_crc) {
      this->valid_[s] = false;
      continue;
    }
    this->header_[s] = h;
    if (h.seq >= this->next_seq_)
      this->next_seq_ = h.seq + 1;
    ESP_LOGI(TAG, "Stored: %s %u (slot %d)", h.key, h.year, s);
  }
  return true;
}

bool PrayerStore::read_header_(int slot, Header *h) const {
  if (esp_partition_read(this->part_, slot * SLOT_SIZE, h, sizeof(Header)) != ESP_OK)
    return false;
  if (h->magic != MAGIC || h->version != VERSION)
    return false;
  if (esp_rom_crc32_le(0, reinterpret_cast<const uint8_t *>(h), offsetof(Header, header_crc)) != h->header_crc)
    return false;
  if ((h->days != 365 && h->days != 366) || h->days * NUM_FIELDS * 2 > SLOT_SIZE - TABLE_OFFSET)
    return false;
  h->key[sizeof(h->key) - 1] = '\0';
  h->tz[sizeof(h->tz) - 1] = '\0';
  return true;
}

int PrayerStore::find(const std::string &key, int year) const {
  std::lock_guard<std::mutex> lock(this->mutex_);
  int best = -1;
  for (int s = 0; s < SLOTS; s++) {
    if (this->valid_[s] && this->header_[s].year == year && key == this->header_[s].key) {
      if (best < 0 || this->header_[s].seq > this->header_[best].seq)
        best = s;
    }
  }
  return best;
}

bool PrayerStore::load(int slot, std::vector<uint16_t> *table, std::string *tz) const {
  Header h;
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (slot < 0 || slot >= SLOTS || !this->valid_[slot])
      return false;
    h = this->header_[slot];
  }
  table->resize(h.days * NUM_FIELDS);
  if (esp_partition_read(this->part_, slot * SLOT_SIZE + TABLE_OFFSET, table->data(), table->size() * 2) != ESP_OK)
    return false;
  *tz = h.tz;
  return true;
}

bool PrayerStore::write(const std::string &key, int year, const std::string &tz, const std::vector<uint16_t> &table,
                        const std::vector<std::pair<std::string, int>> &keep) {
  if (this->part_ == nullptr || table.empty() || key.size() >= 32 || tz.size() >= 64)
    return false;
  int target = -1;
  int replaced = -1;
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    // 1. a free slot
    for (int s = 0; s < SLOTS && target < 0; s++)
      if (!this->valid_[s])
        target = s;
    // 2. otherwise the oldest slot that is not one of the years to keep
    if (target < 0) {
      for (int s = 0; s < SLOTS; s++) {
        bool protect = false;
        for (auto &k : keep)
          if (k.first == this->header_[s].key && k.second == this->header_[s].year && !(k.first == key && k.second == year))
            protect = true;
        if (protect)
          continue;
        if (target < 0 || this->header_[s].seq < this->header_[target].seq)
          target = s;
      }
    }
    if (target < 0)
      return false;
    for (int s = 0; s < SLOTS; s++)
      if (s != target && this->valid_[s] && this->header_[s].year == year && key == this->header_[s].key)
        replaced = s;
    this->valid_[target] = false;
  }

  const uint32_t base = target * SLOT_SIZE;
  if (esp_partition_erase_range(this->part_, base, SLOT_SIZE) != ESP_OK)
    return false;
  const size_t bytes = table.size() * 2;
  if (esp_partition_write(this->part_, base + TABLE_OFFSET, table.data(), bytes) != ESP_OK)
    return false;
  Header h{};
  h.magic = MAGIC;
  h.version = VERSION;
  h.days = static_cast<uint16_t>(table.size() / NUM_FIELDS);
  h.year = static_cast<uint16_t>(year);
  std::strncpy(h.key, key.c_str(), sizeof(h.key) - 1);
  std::strncpy(h.tz, tz.c_str(), sizeof(h.tz) - 1);
  h.table_crc = esp_rom_crc32_le(0, reinterpret_cast<const uint8_t *>(table.data()), bytes);
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    h.seq = this->next_seq_++;
  }
  h.header_crc = esp_rom_crc32_le(0, reinterpret_cast<const uint8_t *>(&h), offsetof(Header, header_crc));
  if (esp_partition_write(this->part_, base, &h, sizeof(h)) != ESP_OK)
    return false;
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->header_[target] = h;
    this->valid_[target] = true;
  }
  // An older copy of the same year is now redundant: free its slot (erasing the header sector is enough).
  if (replaced >= 0) {
    {
      std::lock_guard<std::mutex> lock(this->mutex_);
      this->valid_[replaced] = false;
    }
    esp_partition_erase_range(this->part_, replaced * SLOT_SIZE, 4096);
  }
  ESP_LOGI(TAG, "Stored %s %d in slot %d", key.c_str(), year, target);
  return true;
}

std::string PrayerStore::years_of(const std::string &key) const {
  std::lock_guard<std::mutex> lock(this->mutex_);
  std::vector<int> years;
  for (int s = 0; s < SLOTS; s++)
    if (this->valid_[s] && key == this->header_[s].key)
      years.push_back(this->header_[s].year);
  std::sort(years.begin(), years.end());
  years.erase(std::unique(years.begin(), years.end()), years.end());
  std::string out;
  for (int y : years) {
    if (!out.empty())
      out += ", ";
    out += std::to_string(y);
  }
  return out;
}

std::string PrayerStore::any_tz(const std::string &key) const {
  std::lock_guard<std::mutex> lock(this->mutex_);
  int best = -1;
  for (int s = 0; s < SLOTS; s++)
    if (this->valid_[s] && key == this->header_[s].key && (best < 0 || this->header_[s].year > this->header_[best].year))
      best = s;
  return best < 0 ? std::string() : std::string(this->header_[best].tz);
}

}  // namespace athan
}  // namespace esphome
