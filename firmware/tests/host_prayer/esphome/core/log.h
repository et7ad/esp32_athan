#pragma once
// Host stand-in for ESPHome's log.h (test_year_files.cpp only): the log calls of prayer_store.cpp do nothing.
#define ESP_LOGE(tag, ...) ((void) (tag))
#define ESP_LOGW(tag, ...) ((void) (tag))
#define ESP_LOGI(tag, ...) ((void) (tag))
#define ESP_LOGD(tag, ...) ((void) (tag))
#define ESP_LOGV(tag, ...) ((void) (tag))
