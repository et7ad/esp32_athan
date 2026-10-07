#include "audio_slots.h"

#include <cstddef>
#include <cstring>

#include "esphome/core/log.h"

#include "mp3_check.h"

#include <esp_heap_caps.h>
#include <esp_rom_crc.h>

namespace esphome {
namespace athan {

static const char *const TAG = "athan.slots";

static const uint32_t REGION_OFFSET[NUM_SLOTS] = {0x000000, 0x2E0000, 0x5C0000, 0x8A0000};
static const uint32_t REGION_SIZE[NUM_SLOTS] = {0x2E0000, 0x2E0000, 0x2E0000, 0x070000};
static const uint32_t HEADER_SECTOR = 4096;
static const uint32_t MAGIC = 0x31485441;  // "ATH1"
static const uint32_t VERSION = 2;  // 2 added source_crc; version 1 headers are still read
static const esp_partition_subtype_t AUDIO_SUBTYPE = static_cast<esp_partition_subtype_t>(0x40);

bool AudioSlots::begin() {
  this->part_ = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, AUDIO_SUBTYPE, "audio");
  if (this->part_ == nullptr) {
    ESP_LOGE(TAG, "No 'audio' partition: flash the partition table from firmware/partitions.csv over USB once");
    return false;
  }
  if (this->part_->size < REGION_OFFSET[SLOT_TICK] + REGION_SIZE[SLOT_TICK]) {
    ESP_LOGE(TAG, "'audio' partition is %u bytes, too small", (unsigned) this->part_->size);
    this->part_ = nullptr;
    return false;
  }
  for (int s = 0; s < NUM_SLOTS; s++) {
    this->valid_[s] = this->check_slot_(s);
    if (this->valid_[s]) {
      ESP_LOGI(TAG, "Slot %s: '%s', %u bytes, %u s, %u Hz, %u ch", SLOT_NAMES[s], this->header_[s].label,
               (unsigned) this->header_[s].length, (unsigned) (this->header_[s].duration_ms / 1000),
               (unsigned) this->rate_[s], (unsigned) this->channels_[s]);
    } else {
      ESP_LOGI(TAG, "Slot %s: empty", SLOT_NAMES[s]);
    }
  }
  return true;
}

uint32_t AudioSlots::source_id(const std::string &url) {
  uint32_t crc = esp_rom_crc32_le(0, reinterpret_cast<const uint8_t *>(url.data()), url.size());
  return crc == 0 ? 1 : crc;
}

std::string AudioSlots::label(int slot) const {
  if (!this->valid(slot))
    return "";
  return std::string(this->header_[slot].label, strnlen(this->header_[slot].label, sizeof(this->header_[slot].label)));
}

bool AudioSlots::read_header_(int slot, Header *h) const {
  if (esp_partition_read(this->part_, REGION_OFFSET[slot], h, sizeof(Header)) != ESP_OK)
    return false;
  if (h->magic != MAGIC)
    return false;
  if (h->version == 1) {
    // Version 1 stopped after label: its CRC sits where source_crc is now.
    uint32_t crc = esp_rom_crc32_le(0, reinterpret_cast<const uint8_t *>(h), offsetof(Header, source_crc));
    if (crc != h->source_crc)
      return false;
    h->source_crc = 0;
  } else if (h->version == VERSION) {
    uint32_t crc = esp_rom_crc32_le(0, reinterpret_cast<const uint8_t *>(h), offsetof(Header, header_crc));
    if (crc != h->header_crc)
      return false;
  } else {
    return false;
  }
  if (h->length == 0 || h->length > SLOT_MAX_BYTES[slot] || h->length + HEADER_SECTOR > REGION_SIZE[slot])
    return false;
  return true;
}

const uint8_t *AudioSlots::map_(int slot) {
  if (this->map_ptr_[slot] != nullptr)
    return this->map_ptr_[slot];
  const void *ptr = nullptr;
  // The whole region stays mapped for the life of the program. Flash writes flush the cache for the range they
  // touch, so a later rewrite of the same slot is seen through this mapping.
  esp_err_t err = esp_partition_mmap(this->part_, REGION_OFFSET[slot] + HEADER_SECTOR,
                                     REGION_SIZE[slot] - HEADER_SECTOR, ESP_PARTITION_MMAP_DATA, &ptr,
                                     &this->map_handle_[slot]);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "mmap of slot %s failed: %s", SLOT_NAMES[slot], esp_err_to_name(err));
    return nullptr;
  }
  this->map_ptr_[slot] = static_cast<const uint8_t *>(ptr);
  return this->map_ptr_[slot];
}

bool AudioSlots::check_slot_(int slot) {
  Header h;
  if (!this->read_header_(slot, &h))
    return false;
  const uint8_t *data = this->map_(slot);
  if (data == nullptr)
    return false;
  uint32_t crc = esp_rom_crc32_le(0, data, h.length);
  if (crc != h.data_crc) {
    ESP_LOGW(TAG, "Slot %s: data CRC mismatch, treating it as empty", SLOT_NAMES[slot]);
    return false;
  }
  this->header_[slot] = h;
  this->header_[slot].label[sizeof(h.label) - 1] = '\0';
  // Hand the player the file from its first real frame: a decoder that syncs on a false header inside a tag or
  // junk takes that header's sample rate for the whole file (wrong speed and pitch). Also note the real format.
  Mp3Info info = mp3_scan(data, h.length);
  const size_t skip = (info.ok && info.first_frame < h.length) ? info.first_frame : 0;
  this->rate_[slot] = info.ok ? info.sample_rate : 0;
  this->channels_[slot] = info.ok ? info.channels : 0;
  this->file_[slot].data = data + skip;
  this->file_[slot].length = h.length - skip;
#ifdef USE_AUDIO_MP3_SUPPORT
  this->file_[slot].file_type = audio::AudioFileType::MP3;
#endif
  return true;
}

audio::AudioFile *AudioSlots::file(int slot) {
  if (!this->valid(slot))
    return nullptr;
  return &this->file_[slot];
}

void AudioSlots::invalidate(int slot) {
  if (slot >= 0 && slot < NUM_SLOTS)
    this->valid_[slot] = false;
}

void AudioSlots::reload(int slot) {
  if (slot < 0 || slot >= NUM_SLOTS || this->part_ == nullptr)
    return;
  this->valid_[slot] = this->check_slot_(slot);
}

bool AudioSlots::write(int slot, const uint8_t *data, size_t len, uint32_t duration_ms, const std::string &label,
                       uint32_t source, void (*progress)(void *ctx, int percent), void *ctx) {
  if (this->part_ == nullptr || slot < 0 || slot >= NUM_SLOTS || len == 0 || len > SLOT_MAX_BYTES[slot])
    return false;
  const uint32_t base = REGION_OFFSET[slot];
  // Erase the header sector first, then the data sectors the file needs: the slot reads as empty from now on.
  uint32_t erase_len = (HEADER_SECTOR + len + 4095) & ~4095u;
  esp_err_t err = esp_partition_erase_range(this->part_, base, erase_len);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Erase failed: %s", esp_err_to_name(err));
    return false;
  }
  // Flash writes from PSRAM go through a small internal-RAM buffer (the flash driver disables the cache that
  // PSRAM sits behind while it writes).
  const size_t CHUNK = 4096;
  uint8_t *bounce = static_cast<uint8_t *>(heap_caps_malloc(CHUNK, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (bounce == nullptr)
    return false;
  uint32_t crc = 0;
  int last_pct = -1;
  for (size_t off = 0; off < len; off += CHUNK) {
    size_t n = (len - off < CHUNK) ? len - off : CHUNK;
    std::memcpy(bounce, data + off, n);
    crc = esp_rom_crc32_le(crc, bounce, n);
    err = esp_partition_write(this->part_, base + HEADER_SECTOR + off, bounce, n);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Write failed at %u: %s", (unsigned) off, esp_err_to_name(err));
      heap_caps_free(bounce);
      return false;
    }
    int pct = static_cast<int>((off + n) * 100 / len);
    if (progress != nullptr && pct != last_pct && pct % 5 == 0) {
      progress(ctx, pct);
      last_pct = pct;
    }
  }
  heap_caps_free(bounce);

  Header h{};
  h.magic = MAGIC;
  h.version = VERSION;
  h.length = len;
  h.data_crc = crc;
  h.duration_ms = duration_ms;
  std::strncpy(h.label, label.c_str(), sizeof(h.label) - 1);
  h.source_crc = source;
  h.header_crc = esp_rom_crc32_le(0, reinterpret_cast<const uint8_t *>(&h), offsetof(Header, header_crc));
  err = esp_partition_write(this->part_, base, &h, sizeof(h));
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Header write failed: %s", esp_err_to_name(err));
    return false;
  }
  ESP_LOGI(TAG, "Slot %s written: '%s', %u bytes", SLOT_NAMES[slot], h.label, (unsigned) len);
  return true;
}

}  // namespace athan
}  // namespace esphome
