#pragma once
// Host stand-in for ESPHome's json_util.h (test_year_files.cpp only): parse_json() as ESPHome 2026.9.1 has it
// (components/json/json_util.cpp): an unbound object for no data or a parse error, else the document.
#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace json {
inline JsonDocument parse_json(const uint8_t *data, size_t len) {
  if (data == nullptr || len == 0)
    return JsonObject();
  JsonDocument doc;
  if (deserializeJson(doc, data, len) != DeserializationError::Ok)
    return JsonObject();
  return doc;
}
}  // namespace json
}  // namespace esphome
