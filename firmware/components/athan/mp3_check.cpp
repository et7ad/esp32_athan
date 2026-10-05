#include "mp3_check.h"

#include <cstring>

namespace esphome {
namespace athan {

namespace {

struct Frame {
  uint32_t length;       // bytes, header included
  uint32_t samples;      // per channel
  uint32_t sample_rate;  // Hz
  uint8_t channels;
};

// Bitrates (kbps) for Layer III. Index 0 = free format (rejected), 15 = invalid.
const uint16_t BITRATE_V1_L3[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
const uint16_t BITRATE_V2_L3[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
const uint32_t RATE_V1[3] = {44100, 48000, 32000};

bool parse_frame(const uint8_t *h, Frame *f) {
  if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0)
    return false;
  uint8_t version = (h[1] >> 3) & 0x03;  // 0 = MPEG 2.5, 1 = reserved, 2 = MPEG 2, 3 = MPEG 1
  uint8_t layer = (h[1] >> 1) & 0x03;    // 1 = Layer III
  uint8_t bitrate_idx = (h[2] >> 4) & 0x0F;
  uint8_t rate_idx = (h[2] >> 2) & 0x03;
  uint8_t padding = (h[2] >> 1) & 0x01;
  uint8_t mode = (h[3] >> 6) & 0x03;
  if (version == 1 || layer != 1 || bitrate_idx == 0 || bitrate_idx == 15 || rate_idx == 3)
    return false;
  bool v1 = (version == 3);
  uint32_t bitrate = (v1 ? BITRATE_V1_L3[bitrate_idx] : BITRATE_V2_L3[bitrate_idx]) * 1000u;
  uint32_t rate = RATE_V1[rate_idx];
  if (version == 2)
    rate /= 2;  // MPEG 2
  else if (version == 0)
    rate /= 4;  // MPEG 2.5
  f->samples = v1 ? 1152 : 576;
  f->length = (v1 ? 144u : 72u) * bitrate / rate + padding;
  f->sample_rate = rate;
  f->channels = (mode == 3) ? 1 : 2;
  return f->length >= 4;
}

size_t skip_id3v2(const uint8_t *data, size_t len, size_t pos) {
  // A file may carry several ID3v2 tags back to back.
  while (pos + 10 <= len && std::memcmp(data + pos, "ID3", 3) == 0) {
    const uint8_t *t = data + pos;
    if ((t[6] | t[7] | t[8] | t[9]) & 0x80)
      break;  // not a synchsafe size: not a tag
    size_t size = (static_cast<size_t>(t[6]) << 21) | (static_cast<size_t>(t[7]) << 14) |
                  (static_cast<size_t>(t[8]) << 7) | t[9];
    size_t total = 10 + size + ((t[5] & 0x10) ? 10 : 0);  // footer flag
    pos += total;
  }
  return pos;
}

}  // namespace

Mp3Info mp3_scan(const uint8_t *data, size_t len) {
  Mp3Info info;
  if (data == nullptr || len < 64) {
    info.error = "file too small";
    return info;
  }
  size_t pos = skip_id3v2(data, len, 0);
  if (pos >= len) {
    info.error = "only a tag, no audio";
    return info;
  }

  // Find the first frame whose next two frames chain (limits false syncs). Search at most 64 KB.
  size_t search_end = (len - pos > 65536) ? pos + 65536 : len;
  bool found = false;
  for (; pos + 4 <= search_end; pos++) {
    Frame a, b, c;
    if (!parse_frame(data + pos, &a))
      continue;
    size_t p2 = pos + a.length;
    if (p2 + 4 > len || !parse_frame(data + p2, &b))
      continue;
    size_t p3 = p2 + b.length;
    if (p3 + 4 <= len && !parse_frame(data + p3, &c))
      continue;
    if (a.sample_rate != b.sample_rate)
      continue;
    found = true;
    break;
  }
  if (!found) {
    info.error = "not an MP3 file";
    return info;
  }
  info.first_frame = pos;

  uint64_t total_samples = 0;
  Frame f;
  bool first = true;
  while (pos + 4 <= len) {
    if (!parse_frame(data + pos, &f)) {
      // Trailing tags (ID3v1 "TAG", APE "APETAGEX") or garbage: stop counting there.
      break;
    }
    if (pos + f.length > len) {
      break;  // truncated last frame: ignore it
    }
    if (first) {
      info.sample_rate = f.sample_rate;
      info.channels = f.channels;
      first = false;
    }
    if (f.sample_rate == info.sample_rate) {
      total_samples += f.samples;
      info.frames++;
    }
    pos += f.length;
  }
  if (info.frames < 2 || info.sample_rate == 0) {
    info.error = "no playable MP3 frames";
    return info;
  }
  info.duration_ms = static_cast<uint32_t>(total_samples * 1000ull / info.sample_rate);
  info.ok = true;
  return info;
}

}  // namespace athan
}  // namespace esphome
