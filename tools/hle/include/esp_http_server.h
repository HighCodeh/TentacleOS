#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int httpd_handle_t;
typedef int httpd_req_t;
typedef int httpd_config_t;
typedef int httpd_uri_t;
typedef int httpd_method_t;
typedef int ssize_t;

#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){0})
#define HTTPD_200 "200 OK"
#define HTTPD_400 "400 Bad Request"
#define HTTPD_500 "500 Internal Server Error"
#define HTTPD_TYPE_TEXT "text/plain"
#define HTTPD_TYPE_HTML "text/html"
#define ESP_ERR_HTTPD_BASE 0x8000

esp_err_t httpd_start(httpd_handle_t *h, const httpd_config_t *cfg);
esp_err_t httpd_stop(httpd_handle_t h);
esp_err_t httpd_register_uri_handler(httpd_handle_t h, const httpd_uri_t *uri);
esp_err_t httpd_resp_send(httpd_req_t *req, const char *buf, ssize_t len);
esp_err_t httpd_req_get_url_query_str(httpd_req_t *req, char *buf, size_t len);
esp_err_t httpd_query_key_value(const char *query, const char *key, char *val, size_t len);

#ifdef __cplusplus
}
#endif
