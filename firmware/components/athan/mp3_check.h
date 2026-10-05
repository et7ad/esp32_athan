#pragma once
// MP3 validation and duration, for files received over the network or uploaded from the web page.
// Plain C++ with no ESPHome or ESP-IDF includes, so it can be unit-tested on a computer.

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace athan {

struct Mp3Info {
  bool ok{false};
  const char *error{nullptr};  // set when ok == false
  uint32_t frames{0};
  uint32_t duration_ms{0};
  uint32_t sample_rate{0};
  uint8_t channels{0};
  size_t first_frame{0};  // byte offset of the first audio frame (after an ID3v2 tag)
};

/// Walks every MPEG audio Layer III frame and sums their durations. Skips leading ID3v2 tags and stops at a
/// trailing ID3v1/APE tag or at the first bytes that are not a frame. Rejects data whose first frames do not
/// chain (to avoid false sync words in non-MP3 files).
Mp3Info mp3_scan(const uint8_t *data, size_t len);

}  // namespace athan
}  // namespace esphome
