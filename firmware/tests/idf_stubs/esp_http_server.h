#pragma once
#include "idf_common.h"
typedef enum http_method { HTTP_DELETE = 0, HTTP_GET = 1, HTTP_HEAD = 2, HTTP_POST = 3, HTTP_PUT = 4, HTTP_OPTIONS = 6, HTTP_PATCH = 28 } http_method;
typedef void *httpd_handle_t;
typedef struct httpd_req { httpd_handle_t handle; int method; const char uri[512 + 1]; size_t content_len; void *aux; void *user_ctx; void *sess_ctx; } httpd_req_t;
#define HTTPD_RESP_USE_STRLEN -1
typedef enum { HTTPD_500_INTERNAL_SERVER_ERROR = 0, HTTPD_400_BAD_REQUEST, HTTPD_404_NOT_FOUND, HTTPD_408_REQ_TIMEOUT } httpd_err_code_t;
esp_err_t httpd_resp_send(httpd_req_t *, const char *, ssize_t);
esp_err_t httpd_resp_send_err(httpd_req_t *, httpd_err_code_t, const char *);
esp_err_t httpd_resp_set_status(httpd_req_t *, const char *);
esp_err_t httpd_resp_set_type(httpd_req_t *, const char *);
esp_err_t httpd_resp_set_hdr(httpd_req_t *, const char *, const char *);
int httpd_req_recv(httpd_req_t *, char *, size_t);
typedef struct { int x; } httpd_config_t;
