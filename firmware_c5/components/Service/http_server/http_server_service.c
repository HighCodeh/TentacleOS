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

#include "http_server_service.h"

#include <stdbool.h>

#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"

#include "storage_impl.h"

static const char *TAG = "HTTP_SERVICE";
static const char *TAG_SD = "HTTP_SDCARD";

static httpd_handle_t s_server = NULL;

esp_err_t start_web_server(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.lru_purge_enable = true;
  // Wildcard matching lets a single "/*" handler answer every path (needed so a
  // captive portal serves its page on the OS probe URLs like /generate_204).
  config.uri_match_fn = httpd_uri_match_wildcard;
  // Short recv/send timeouts free a stalled client's socket fast so captive-portal
  // probes are not left waiting behind it. Socket count stays at the default:
  // internal DRAM is scarce, and the DNS honeypot keeps only a few clients talking
  // to us, so more sockets would just cost RAM the HTTP server needs.
  config.recv_wait_timeout = 3;
  config.send_wait_timeout = 3;

  if (httpd_start(&s_server, &config) == ESP_OK) {
    ESP_LOGI(TAG, "Serviço HTTP iniciado.");
    return ESP_OK;
  }
  ESP_LOGE(TAG, "Falha ao iniciar serviço HTTP.");
  return ESP_FAIL;
}

esp_err_t http_service_register_uri(const httpd_uri_t *uri_handler) {
  if (s_server) {
    ESP_LOGI(TAG, "Serviço HTTP registrando URI: %s", uri_handler->uri);
    return httpd_register_uri_handler(s_server, uri_handler);
  }
  ESP_LOGE(TAG, "Servidor não iniciado, impossível registrar URI '%s'", uri_handler->uri);
  return ESP_FAIL;
}

esp_err_t http_service_register_404_handler(httpd_err_handler_func_t fn) {
  if (s_server == NULL) {
    ESP_LOGE(TAG, "Servidor não iniciado, impossível registrar handler 404");
    return ESP_FAIL;
  }
  return httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, fn);
}

esp_err_t stop_http_server(void) {
  if (s_server == NULL) {
    ESP_LOGW(TAG, "Servidor HTTP já está parado ou não foi iniciado.");
    return ESP_OK;
  }

  esp_err_t err = httpd_stop(s_server);
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Servidor HTTP parado com sucesso.");
    s_server = NULL;
  } else {
    ESP_LOGE(TAG, "Falha ao parar o servidor HTTP: %s", esp_err_to_name(err));
  }

  return err;
}

const char *get_html_buffer(const char *path) {
  size_t file_size = 0;

  esp_err_t err = storage_file_get_size(path, &file_size);

  if (err != ESP_OK) {
    ESP_LOGE(TAG_SD, "Erro ao obter tamanho do arquivo: %s (%s)", path, esp_err_to_name(err));
    return NULL;
  }

  if (file_size == 0) {
    ESP_LOGE(TAG_SD, "Arquivo vazio: %s", path);
    return NULL;
  }

  char *buffer = (char *)malloc(file_size + 1);
  if (buffer == NULL) {
    ESP_LOGE(TAG_SD, "Falha na alocação (OOM)");
    return NULL;
  }

  FILE *f = fopen(path, "rb");
  if (f == NULL) {
    ESP_LOGE(TAG_SD, "Erro ao abrir arquivo: %s", path);
    free(buffer);
    return NULL;
  }

  size_t bytes_read = fread(buffer, 1, file_size, f);
  fclose(f);

  if (bytes_read != file_size) {
    ESP_LOGE(TAG_SD, "Leitura incompleta: %zu/%zu bytes", bytes_read, file_size);
    free(buffer);
    return NULL;
  }

  buffer[file_size] = '\0';

  ESP_LOGI(TAG_SD, "Buffer carregado com sucesso: %zu bytes", file_size);
  return buffer;
}

static httpd_err_code_t map_http_status_to_esp_err(int code) {
  switch (code) {
    case 400:
      return HTTPD_400_BAD_REQUEST;
    case 401:
      return HTTPD_401_UNAUTHORIZED;
    case 403:
      return HTTPD_403_FORBIDDEN;
    case 404:
      return HTTPD_404_NOT_FOUND;
    case 408:
      return HTTPD_408_REQ_TIMEOUT;
    case 500:
      return HTTPD_500_INTERNAL_SERVER_ERROR;
    default:
      return HTTPD_500_INTERNAL_SERVER_ERROR;
  }
}

esp_err_t http_service_send_error(httpd_req_t *req, http_status_t status_code, const char *msg) {
  httpd_err_code_t error_code = map_http_status_to_esp_err(status_code);
  return httpd_resp_send_err(req, error_code, msg);
}

esp_err_t http_service_req_recv(httpd_req_t *req, char *buffer, size_t buffer_size) {
  if (req->content_len >= buffer_size) {
    ESP_LOGE(TAG, "Tamanho do conteúdo (%d) maior que buffer (%d)", req->content_len, buffer_size);
    http_service_send_error(req, HTTP_STATUS_BAD_REQUEST_400, "Request content too long");
    return ESP_ERR_INVALID_SIZE;
  }

  int ret = httpd_req_recv(req, buffer, req->content_len);

  if (ret <= 0) {
    if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
      http_service_send_error(req, HTTP_STATUS_REQUEST_TIMEOUT_408, "Timeout receiving data");
    }
    return ESP_FAIL;
  }

  buffer[ret] = '\0';
  return ESP_OK;
}

esp_err_t http_service_query_key_value(const char *data_buffer,
                                       const char *key,
                                       char *out_val,
                                       size_t out_size) {
  esp_err_t err = httpd_query_key_value(data_buffer, key, out_val, out_size);

  if (err == ESP_OK) {
    return ESP_OK;
  } else if (err == ESP_ERR_NOT_FOUND) {
    ESP_LOGD(TAG, "Chave '%s' não encontrada no buffer", key);
  } else if (err == ESP_ERR_HTTPD_RESULT_TRUNC) {
    ESP_LOGE(TAG, "Valor da chave '%s' foi truncado (buffer pequeno)", key);
  }

  return err;
}

esp_err_t http_service_send_response(httpd_req_t *req, const char *buffer, ssize_t length) {
  if (buffer == NULL) {
    ESP_LOGW(TAG, "Tentativa de enviar buffer NULL. Enviando erro.");
    return http_service_send_error(
        req, HTTP_STATUS_INTERNAL_ERROR_500, "Internal Server Error: No content generated");
  }

  esp_err_t err = httpd_resp_send(req, buffer, length);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Erro ao enviar resposta HTTP: %s", esp_err_to_name(err));
  } else {
    ESP_LOGD(TAG, "Resposta enviada com sucesso (%d bytes)", length);
  }

  return err;
}

esp_err_t http_service_send_file_from_sd(httpd_req_t *req, const char *filepath) {
  const char *html_content = get_html_buffer(filepath);

  if (html_content == NULL) {
    return http_service_send_error(req, HTTP_STATUS_NOT_FOUND_404, "File not found or empty");
  }

  esp_err_t err = http_service_send_response(req, html_content, HTTPD_RESP_USE_STRLEN);

  free((void *)html_content);
  return err;
}
