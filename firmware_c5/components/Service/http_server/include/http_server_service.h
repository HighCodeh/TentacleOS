// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#ifndef HTTP_SERVER_SERVICE_H
#define HTTP_SERVER_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "esp_http_server.h"

typedef enum {
  HTTP_STATUS_OK_200 = 200,
  HTTP_STATUS_CREATED_201 = 201,
  HTTP_STATUS_BAD_REQUEST_400 = 400,
  HTTP_STATUS_UNAUTHORIZED_401 = 401,
  HTTP_STATUS_FORBIDDEN_403 = 403,
  HTTP_STATUS_NOT_FOUND_404 = 404,
  HTTP_STATUS_REQUEST_TIMEOUT_408 = 408,
  HTTP_STATUS_INTERNAL_ERROR_500 = 500,
} http_status_t;

/**
 * @brief Start the HTTP web server.
 *
 * @return ESP_OK on success, ESP_FAIL if the server could not start.
 */
esp_err_t start_web_server(void);

/**
 * @brief Stop the HTTP web server.
 *
 * @return ESP_OK on success.
 */
esp_err_t stop_web_server(void);

/**
 * @brief Register a URI handler with the running server.
 *
 * @param uri_handler Pointer to the URI handler descriptor.
 * @return ESP_OK on success, ESP_FAIL if the server is not started.
 */
esp_err_t http_service_register_uri(const httpd_uri_t *uri_handler);

/**
 * @brief Install a handler for unmatched requests (HTTP 404).
 *
 * A captive portal uses this to redirect every OS connectivity probe
 * (generate_204, ncsi.txt, connecttest.txt, ...) to the portal page.
 *
 * @param fn 404 handler, or NULL to clear.
 * @return ESP_OK on success, ESP_FAIL if the server is not started.
 */
esp_err_t http_service_register_404_handler(httpd_err_handler_func_t fn);

/**
 * @brief Stop the HTTP server (alias).
 *
 * @return ESP_OK on success.
 */
esp_err_t stop_http_server(void);

/**
 * @brief Send an HTTP error response.
 *
 * @param req     The HTTP request handle.
 * @param status_code HTTP status code to send.
 * @param msg     Error message string.
 * @return ESP_OK on success.
 */
esp_err_t http_service_send_error(httpd_req_t *req, http_status_t status_code, const char *msg);

/**
 * @brief Receive the request body into a buffer.
 *
 * @param req         The HTTP request handle.
 * @param buffer      Destination buffer.
 * @param buffer_size Size of the destination buffer.
 * @return ESP_OK on success.
 */
esp_err_t http_service_req_recv(httpd_req_t *req, char *buffer, size_t buffer_size);

/**
 * @brief Extract a key-value pair from a query string.
 *
 * @param data_buffer The query string buffer.
 * @param key         Key to search for.
 * @param out_val     Buffer to receive the value.
 * @param out_size    Size of the output buffer.
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if key missing.
 */
esp_err_t http_service_query_key_value(const char *data_buffer,
                                       const char *key,
                                       char *out_val,
                                       size_t out_size);

/**
 * @brief Send an HTTP response with the given buffer.
 *
 * @param req    The HTTP request handle.
 * @param buffer Response body.
 * @param length Length of the response, or HTTPD_RESP_USE_STRLEN.
 * @return ESP_OK on success.
 */
esp_err_t http_service_send_response(httpd_req_t *req, const char *buffer, ssize_t length);

/**
 * @brief Send a file from the SD card as the HTTP response.
 *
 * @param req      The HTTP request handle.
 * @param filepath Path to the file on the SD card.
 * @return ESP_OK on success.
 */
esp_err_t http_service_send_file_from_sd(httpd_req_t *req, const char *filepath);

/**
 * @brief Load a file into a heap-allocated buffer.
 *
 * @param path Path to the file.
 * @return Pointer to the buffer (caller must free), or NULL on error.
 */
const char *get_html_buffer(const char *path);

#ifdef __cplusplus
}
#endif

#endif // HTTP_SERVER_SERVICE_H
