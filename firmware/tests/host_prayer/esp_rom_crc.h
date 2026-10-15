#pragma once
// Host stand-in for ESP-IDF's esp_rom_crc.h (test_year_files.cpp only); the test defines the function.
#include <cstdint>

extern "C" uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t *buf, uint32_t len);
