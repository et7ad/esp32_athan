#pragma once
#include "idf_common.h"
[[noreturn]] void esp_system_abort(const char *details);
void esp_restart();
uint32_t esp_get_free_heap_size();
