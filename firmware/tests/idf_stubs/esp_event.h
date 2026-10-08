#pragma once
#include "idf_common.h"
esp_err_t esp_event_handler_instance_register(esp_event_base_t event_base, int32_t event_id,
                                              esp_event_handler_t event_handler, void *event_handler_arg,
                                              esp_event_handler_instance_t *instance);
#define ESP_EVENT_ANY_ID -1
