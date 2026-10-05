#pragma once
// Host stand-ins for ESP-IDF declarations, for firmware/tests/syntax_check.sh only (a type check of the
// custom component and the yaml lambdas on a computer). NOT used by the real build; only what ESPHome headers need.
#include <cstdint>
#include <cstddef>
#include <cstdlib>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_TIMEOUT 0x107
inline const char *esp_err_to_name(esp_err_t) { return ""; }
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
typedef void *SemaphoreHandle_t;
typedef void *QueueHandle_t;
typedef void *EventGroupHandle_t;
typedef void *RingbufHandle_t;
typedef uint32_t EventBits_t;
typedef void (*TaskFunction_t)(void *);
typedef struct StaticTask_t { int x; } StaticTask_t;
typedef struct StaticQueue_t { int x; } StaticQueue_t;
typedef StaticQueue_t StaticSemaphore_t;
typedef struct StaticEventGroup_t { int x; } StaticEventGroup_t;
typedef struct StaticRingbuffer_t { int x; } StaticRingbuffer_t;
typedef int StackType_t;
#define pdPASS 1
#define pdFAIL 0
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 0xffffffffUL
#define portTICK_PERIOD_MS 1
#define pdMS_TO_TICKS(x) ((TickType_t) (x))
#define configMAX_PRIORITIES 25
#define tskNO_AFFINITY 0x7fffffff
#define portMUX_INITIALIZER_UNLOCKED {}
typedef const char *esp_event_base_t;
typedef void *esp_event_handler_instance_t;
typedef void (*esp_event_handler_t)(void *, esp_event_base_t, int32_t, void *);
BaseType_t xPortInIsrContext();
void vPortYield();
