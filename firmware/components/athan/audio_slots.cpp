#include "audio_slots.h"

#include <algorithm>
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
static const uint32_t ERASE_BLOCK = 65536;  // the flash's block: one erase command each
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
    this->map_(s);  // once, here: never mapped or unmapped later (verify() reads it from the worker task)
    this->adopt(s, this->verify(s));
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

AudioSlots::Verified AudioSlots::verify(int slot) const {
  Verified v;
  if (this->part_ == nullptr || slot < 0 || slot >= NUM_SLOTS || !this->read_header_(slot, &v.header))
    return v;
  const uint8_t *data = this->map_ptr_[slot];
  if (data == nullptr)
    return v;
  if (esp_rom_crc32_le(0, data, v.header.length) != v.header.data_crc) {
    ESP_LOGW(TAG, "Slot %s: data CRC mismatch, treating it as empty", SLOT_NAMES[slot]);
    return v;
  }
  v.header.label[sizeof(v.header.label) - 1] = '\0';
  // Hand the player the file from its first real frame: a decoder that syncs on a false header inside a tag or
  // junk takes that header's sample rate for the whole file (wrong speed and pitch). Also note the real format.
  Mp3Info info = mp3_scan(data, v.header.length);
  v.skip = (info.ok && info.first_frame < v.header.length) ? info.first_frame : 0;
  v.rate = info.ok ? info.sample_rate : 0;
  v.channels = info.ok ? info.channels : 0;
  v.ok = true;
  return v;
}

void AudioSlots::adopt(int slot, const Verified &v) {
  if (slot < 0 || slot >= NUM_SLOTS)
    return;
  this->valid_[slot] = v.ok && this->map_ptr_[slot] != nullptr;
  if (!this->valid_[slot])
    return;
  this->header_[slot] = v.header;
  this->rate_[slot] = v.rate;
  this->channels_[slot] = v.channels;
  this->file_[slot].data = this->map_ptr_[slot] + v.skip;
  this->file_[slot].length = v.header.length - v.skip;
#ifdef USE_AUDIO_MP3_SUPPORT
  this->file_[slot].file_type = audio::AudioFileType::MP3;
#endif
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
  this->adopt(slot, this->verify(slot));
}

bool AudioSlots::clear(int slot) {
  if (this->part_ == nullptr || slot < 0 || slot >= NUM_SLOTS)
    return false;
  this->valid_[slot] = false;
  const esp_err_t err = esp_partition_erase_range(this->part_, REGION_OFFSET[slot], HEADER_SECTOR);
  if (err != ESP_OK)
    ESP_LOGE(TAG, "Erase failed: %s", esp_err_to_name(err));
  return err == ESP_OK;
}

bool AudioSlots::write(int slot, const uint8_t *data, size_t len, uint32_t duration_ms, const std::string &label,
                       uint32_t source, void (*progress)(void *ctx, int percent),
                       void (*pace)(void *ctx, uint32_t pause_ms), void *ctx) {
  if (this->part_ == nullptr || slot < 0 || slot >= NUM_SLOTS || len == 0 || len > SLOT_MAX_BYTES[slot])
    return false;
  const uint32_t base = REGION_OFFSET[slot];
  // Erase the header sector first, then the data sectors the file needs: the slot reads as empty from now on.
  // One 64 KB block at a time (along the flash's own blocks): each erase takes 0.1-0.3 s with the cache off on
  // both cores, so all of the clock stood still for the whole file (about 6 s for 2 MB, seen on the prototype, and
  // the I2S speaker restarted). Between blocks the main loop and the audio get their time.
  if (pace != nullptr)
    pace(ctx, 0);
  const uint32_t erase_len = (HEADER_SECTOR + len + 4095) & ~4095u;
  for (uint32_t off = 0; off < erase_len;) {
    const uint32_t at = this->part_->address + base + off;
    const uint32_t n = std::min<uint32_t>(erase_len - off, ERASE_BLOCK - at % ERASE_BLOCK);
    esp_err_t err = esp_partition_erase_range(this->part_, base + off, n);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Erase failed: %s", esp_err_to_name(err));
      return false;
    }
    off += n;
    if (pace != nullptr)
      pace(ctx, 30);
  }
  esp_err_t err = ESP_OK;
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
    if (pace != nullptr && (off / CHUNK) % 4 == 3)
      pace(ctx, 10);  // every 16 KB (about 50 ms of writing)
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
