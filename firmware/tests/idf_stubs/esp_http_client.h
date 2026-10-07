#pragma once
#include "idf_common.h"
typedef struct esp_http_client *esp_http_client_handle_t;
typedef enum { HTTP_EVENT_ERROR = 0, HTTP_EVENT_ON_CONNECTED, HTTP_EVENT_HEADERS_SENT, HTTP_EVENT_ON_HEADER, HTTP_EVENT_ON_DATA, HTTP_EVENT_ON_FINISH, HTTP_EVENT_DISCONNECTED, HTTP_EVENT_REDIRECT } esp_http_client_event_id_t;
typedef struct esp_http_client_event { esp_http_client_event_id_t event_id; esp_http_client_handle_t client; void *data; int data_len; void *user_data; char *header_key; char *header_value; } esp_http_client_event_t;
typedef esp_err_t (*http_event_handle_cb)(esp_http_client_event_t *evt);
typedef struct {
  const char *url; const char *host; int port; const char *username; const char *password; const char *path;
  const char *query; const char *cert_pem; const char *user_agent; int timeout_ms; bool disable_auto_redirect;
  int max_redirection_count; int max_authorization_retries; http_event_handle_cb event_handler; int buffer_size;
  int buffer_size_tx; esp_err_t (*crt_bundle_attach)(void *conf); bool keep_alive_enable; void *user_data;
} esp_http_client_config_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_open(esp_http_client_handle_t, int);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t, const char *key, const char *value);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
esp_err_t esp_http_client_set_redirection(esp_http_client_handle_t);
esp_err_t esp_http_client_close(esp_http_client_handle_t);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);
int esp_http_client_read(esp_http_client_handle_t, char *, int);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t);
