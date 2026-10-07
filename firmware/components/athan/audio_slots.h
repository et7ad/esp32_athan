#pragma once
// The four replaceable sounds, stored raw in the "audio" flash partition (see firmware/partitions.csv).
//
//   slot 0 athan     region 0x000000, 0x2E0000 bytes   (3 MB file + 4 KB header)
//   slot 1 fajr      region 0x2E0000, 0x2E0000 bytes
//   slot 2 tawashih  region 0x5C0000, 0x2E0000 bytes
//   slot 3 tick      region 0x8A0000, 0x070000 bytes   (0.4 MB file + 4 KB header)
//
// Each region = a 4 KB header sector, then the MP3 bytes. A write erases the region, writes the file, then the
// header LAST, so a power cut while writing leaves the slot without a valid header (= empty), never half-valid.
// Playback memory-maps the region and hands it to the media player as an in-memory audio file: no RAM copy.
//
// Threading: write() runs on the component's worker task; everything else on the main loop.

#include <atomic>
#include <cstdint>
#include <string>

#include "esphome/components/audio/audio.h"

#include <esp_partition.h>

namespace esphome {
namespace athan {

static const int NUM_SLOTS = 4;
enum SlotId : int { SLOT_ATHAN = 0, SLOT_FAJR = 1, SLOT_TAWASHIH = 2, SLOT_TICK = 3 };

// Limits checked before anything is written (MB = 1,000,000 bytes, as Finder shows).
static const size_t SLOT_MAX_BYTES[NUM_SLOTS] = {3000000, 3000000, 3000000, 400000};
static const uint32_t SLOT_MAX_MS[NUM_SLOTS] = {300000, 300000, 300000, 60000};
static const char *const SLOT_NAMES[NUM_SLOTS] = {"athan", "fajr", "tawashih", "tick"};

class AudioSlots {
 public:
  /// Finds the partition, reads every header and verifies every file's CRC. Main loop, once.
  bool begin();
  bool ready() const { return this->part_ != nullptr; }

  bool valid(int slot) const { return slot >= 0 && slot < NUM_SLOTS && this->valid_[slot]; }
  std::string label(int slot) const;
  uint32_t duration_ms(int slot) const { return this->valid(slot) ? this->header_[slot].duration_ms : 0; }
  /// CRC-32 of the link the sound was downloaded from (source_id()), 0 for an upload or an unknown source.
  uint32_t source(int slot) const { return this->valid(slot) ? this->header_[slot].source_crc : 0; }
  /// The sound's real format, from its own frames (0 if unknown). Playback compares the pipeline against it.
  uint32_t sample_rate(int slot) const { return this->valid(slot) ? this->rate_[slot] : 0; }
  uint8_t channels(int slot) const { return this->valid(slot) ? this->channels_[slot] : 0; }
  /// Written before the source was recorded (header version 1): only the label identifies the sound.
  bool legacy(int slot) const { return this->valid(slot) && this->header_[slot].version == 1; }
  /// Never 0, so 0 can mean "no link".
  static uint32_t source_id(const std::string &url);

  /// The mapped file for playback, or nullptr if the slot is empty or being rewritten.
  audio::AudioFile *file(int slot);

  /// Main loop: hide the slot before a rewrite starts (playback refused until reload()).
  void invalidate(int slot);
  /// Main loop: re-read the header after a write and re-validate the data.
  void reload(int slot);

  /// Worker task: erase + write + header. `progress` gets 0..100. Returns false on a flash error.
  /// `source` is source_id() of the download link, 0 for an upload.
  bool write(int slot, const uint8_t *data, size_t len, uint32_t duration_ms, const std::string &label,
             uint32_t source, void (*progress)(void *ctx, int percent), void *ctx);

 protected:
  struct Header {
    uint32_t magic;
    uint32_t version;
    uint32_t length;
    uint32_t data_crc;
    uint32_t duration_ms;
    char label[64];
    // Version 2. A version 1 header ends here with its header_crc where source_crc now sits.
    uint32_t source_crc;
    uint32_t header_crc;  // CRC of every field above
  };
  bool read_header_(int slot, Header *h) const;
  bool check_slot_(int slot);
  const uint8_t *map_(int slot);

  const esp_partition_t *part_{nullptr};
  Header header_[NUM_SLOTS]{};
  bool valid_[NUM_SLOTS]{};
  const uint8_t *map_ptr_[NUM_SLOTS]{};
  esp_partition_mmap_handle_t map_handle_[NUM_SLOTS]{};
  audio::AudioFile file_[NUM_SLOTS]{};
  uint32_t rate_[NUM_SLOTS]{};
  uint8_t channels_[NUM_SLOTS]{};
};

}  // namespace athan
}  // namespace esphome
