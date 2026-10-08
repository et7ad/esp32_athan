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
typedef struct {
  uint8_t bssid[6];
  uint8_t ssid[33];
  uint8_t primary;
  int8_t rssi;
} wifi_ap_record_t;
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap_info);
esp_err_t esp_wifi_set_max_tx_power(int8_t power);
esp_err_t esp_wifi_get_max_tx_power(int8_t *power);
typedef enum { WIFI_IF_STA = 0, WIFI_IF_AP = 1 } wifi_interface_t;
#define WIFI_PROTOCOL_11B 0x1
#define WIFI_PROTOCOL_11G 0x2
#define WIFI_PROTOCOL_11N 0x4
esp_err_t esp_wifi_set_protocol(wifi_interface_t ifx, uint8_t protocol_bitmap);
esp_err_t esp_wifi_get_protocol(wifi_interface_t ifx, uint8_t *protocol_bitmap);
