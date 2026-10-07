#pragma once
#include "idf_common.h"
typedef enum { WIFI_PS_NONE, WIFI_PS_MIN_MODEM, WIFI_PS_MAX_MODEM } wifi_ps_type_t;
esp_err_t esp_wifi_set_ps(wifi_ps_type_t type);
extern esp_event_base_t const WIFI_EVENT;
typedef enum { WIFI_EVENT_STA_START = 2, WIFI_EVENT_STA_STOP, WIFI_EVENT_STA_CONNECTED, WIFI_EVENT_STA_DISCONNECTED } wifi_event_t;
typedef struct {
  uint8_t ssid[32];
  uint8_t ssid_len;
  uint8_t bssid[6];
  uint8_t reason;
  int8_t rssi;
} wifi_event_sta_disconnected_t;
