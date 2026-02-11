// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#ifndef HTTP_SERVER_SERVICE_H
#define HTTP_SERVER_SERVICE_H

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

esp_err_t start_web_server(void);
esp_err_t stop_web_server(void);
esp_err_t http_service_register_uri(const httpd_uri_t *uri_handler);

// errors
esp_err_t stop_http_server(void);
esp_err_t http_service_send_error(httpd_req_t *req, http_status_t status_code, const char *msg);

// data receive
esp_err_t  http_service_req_recv(httpd_req_t *req, char *buffer, size_t buffer_size);
esp_err_t http_service_query_key_value(const char *data_buffer, const char *key, char *out_val, size_t out_size);

// data send
esp_err_t http_service_send_response(httpd_req_t *req, const char *buffer, ssize_t length);
esp_err_t http_service_send_file_from_sd(httpd_req_t *req, const char *filepath);

// misc 
const char *get_html_buffer(const char *path);
#endif // !HTTP_SERVER_SERVICE_H
