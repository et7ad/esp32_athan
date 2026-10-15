#pragma once
// Host stand-in for ESP-IDF's esp_partition.h (test_year_files.cpp only): what prayer_store.h/.cpp name. The test
// defines the functions; it only calls parse_year_file(), which needs no flash.
#include <cstddef>
#include <cstdint>

typedef int esp_err_t;
#define ESP_OK 0
typedef enum { ESP_PARTITION_TYPE_APP = 0, ESP_PARTITION_TYPE_DATA = 1 } esp_partition_type_t;
typedef int esp_partition_subtype_t;
typedef struct {
  uint32_t address;
  uint32_t size;
} esp_partition_t;

extern "C" {
const esp_partition_t *esp_partition_find_first(esp_partition_type_t type, esp_partition_subtype_t subtype,
                                                const char *label);
esp_err_t esp_partition_read(const esp_partition_t *part, size_t offset, void *dst, size_t size);
esp_err_t esp_partition_write(const esp_partition_t *part, size_t offset, const void *src, size_t size);
esp_err_t esp_partition_erase_range(const esp_partition_t *part, size_t offset, size_t size);
}
