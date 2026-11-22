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

#include "port_scanner.h"

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "lwip/def.h"

#include "common_ports.h"
#include "led_control.h"
#include "power_manager.h"
#include "port_scanner_store.h"
#include "resource_mgr.h"
#include "spi_bridge.h"
#include "spi_protocol.h"
#include "sys_prio.h"

#define PORT_LIMIT            65535U
#define PORT_BITMAP_BYTES     8192
#define PORT_BATCH_SIZE       32
#define PORT_RPC_MS           2000
#define PORT_POLL_MS          200
#define PORT_BATCH_TIMEOUT_US 45000000
#define PORT_STACK_BYTES      6144
#define PAGE_STACK_BYTES      6144
#define IPV4_BITS             32
#define OCTET_BITS            8
#define DECIMAL_BASE          10U
#define PORT_COUNT_OFFSET     18
#define PORT_LIST_OFFSET      20
#define HOST_YIELD_INTERVAL   4096U

typedef struct {
  port_scanner_config_t config;
  uint8_t ports[PORT_BITMAP_BYTES];
  uint8_t *reachable_hosts;
  uint16_t batch[PORT_BATCH_SIZE];
  uint8_t payload[SPI_MAX_PAYLOAD];
  port_scan_result_t results[PORT_BATCH_SIZE];
  char archive_path[PORT_SCANNER_PATH_LENGTH];
} port_scanner_job_t;

typedef struct {
  port_scanner_browse_request_t request;
  port_scanner_page_t page;
} port_scanner_page_job_t;

static const char *TAG = "PORT_SCANNER_SVC";

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static port_scanner_snapshot_t s_status;
static bool s_is_stop_requested;
// Retained in PSRAM until reboot; a new scan replaces the bounded result set.
static port_scan_result_t *s_results;
static uint16_t s_result_first;
static port_scanner_page_t *s_page_data;
static bool s_is_page_busy;
static bool s_is_page_cancelled;

static void worker(void *argument);
static esp_err_t
target_bounds(const port_scanner_config_t *config, uint32_t *out_first, uint32_t *out_last);
static esp_err_t parse_ports(const char *text, uint8_t *bitmap);
static void on_revoke(void *unused);
static void page_worker(void *argument);
static bool page_cancelled(void);
static void format_host_ip(uint32_t host, char *out_ip);
static esp_err_t discover_hosts(port_scanner_job_t *job, uint32_t first, uint32_t hosts_total);

esp_err_t port_scanner_validate(const port_scanner_config_t *config) {
  if (config == NULL || (unsigned)config->target_mode >= PORT_SCANNER_TARGET_COUNT ||
      (unsigned)config->port_mode >= PORT_SCANNER_PORTS_COUNT ||
      memchr(config->target, '\0', sizeof(config->target)) == NULL ||
      memchr(config->ports, '\0', sizeof(config->ports)) == NULL)
    return ESP_ERR_INVALID_ARG;
  uint32_t first, last;
  if (config->target_mode != PORT_SCANNER_GATEWAY && config->target_mode != PORT_SCANNER_LOCAL &&
      target_bounds(config, &first, &last) != ESP_OK)
    return ESP_ERR_INVALID_ARG;
  return config->port_mode == PORT_SCANNER_CUSTOM_PORTS ? parse_ports(config->ports, NULL) : ESP_OK;
}

void port_scanner_stop(void) {
  portENTER_CRITICAL(&s_lock);
  if (s_status.is_busy) {
    s_is_stop_requested = true;
    s_status.state = PORT_SCANNER_STOPPING;
  }
  portEXIT_CRITICAL(&s_lock);
}

void port_scanner_get_snapshot(port_scanner_snapshot_t *out_snapshot) {
  if (out_snapshot == NULL)
    return;
  portENTER_CRITICAL(&s_lock);
  *out_snapshot = s_status;
  portEXIT_CRITICAL(&s_lock);
}

bool port_scanner_get_result(uint16_t index, port_scan_result_t *out_result) {
  if (out_result == NULL)
    return false;
  portENTER_CRITICAL(&s_lock);
  bool is_valid = s_results != NULL && index < s_status.stored;
  if (is_valid)
    *out_result = s_results[(s_result_first + index) % PORT_SCANNER_RESULTS_MAX];
  portEXIT_CRITICAL(&s_lock);
  return is_valid;
}

esp_err_t port_scanner_request_page(const char *path, uint32_t page) {
  if (path != NULL && strnlen(path, PORT_SCANNER_PATH_LENGTH) == PORT_SCANNER_PATH_LENGTH)
    return ESP_ERR_INVALID_ARG;
  port_scanner_browse_request_t request = {.kind = path == NULL ? PORT_SCANNER_BROWSE_NETWORKS
                                                                : PORT_SCANNER_BROWSE_RECORDS,
                                           .number = page};
  if (path != NULL)
    memcpy(request.path, path, strlen(path) + 1);
  return port_scanner_browse(&request);
}

esp_err_t port_scanner_browse(const port_scanner_browse_request_t *request) {
  if (request == NULL || (unsigned)request->kind >= PORT_SCANNER_BROWSE_COUNT ||
      memchr(request->path, '\0', sizeof(request->path)) == NULL ||
      memchr(request->host.ip, '\0', sizeof(request->host.ip)) == NULL)
    return ESP_ERR_INVALID_ARG;
  portENTER_CRITICAL(&s_lock);
  if (s_status.is_busy || s_is_page_busy) {
    portEXIT_CRITICAL(&s_lock);
    return ESP_ERR_INVALID_STATE;
  }
  s_is_page_busy = true;
  s_is_page_cancelled = false;
  portEXIT_CRITICAL(&s_lock);
  port_scanner_page_job_t *job =
      heap_caps_calloc(1, sizeof(*job), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (job == NULL)
    goto cleanup;
  if (s_page_data == NULL) {
    port_scanner_page_t *data =
        heap_caps_calloc(1, sizeof(*data), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (data == NULL)
      goto cleanup;
    portENTER_CRITICAL(&s_lock);
    s_page_data = data;
    portEXIT_CRITICAL(&s_lock);
  }
  job->request = *request;
  job->page.number = request->number;
  portENTER_CRITICAL(&s_lock);
  *s_page_data = job->page;
  portEXIT_CRITICAL(&s_lock);
  if (xTaskCreatePinnedToCoreWithCaps(page_worker,
                                      "port_page",
                                      PAGE_STACK_BYTES,
                                      job,
                                      SYS_PRIO_SERVICE_LO,
                                      NULL,
                                      SYS_CORE_RADIO,
                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) != pdPASS)
    goto cleanup;
  return ESP_OK;
cleanup:
  ESP_LOGE(TAG, "Archive reader allocation failed");
  led_signal_error();
  free(job);
  portENTER_CRITICAL(&s_lock);
  if (s_page_data != NULL)
    *s_page_data = (port_scanner_page_t){.error = ESP_ERR_NO_MEM};
  s_is_page_busy = false;
  portEXIT_CRITICAL(&s_lock);
  return ESP_ERR_NO_MEM;
}

void port_scanner_get_page(port_scanner_page_t *out_page) {
  if (out_page == NULL)
    return;
  portENTER_CRITICAL(&s_lock);
  *out_page = s_page_data != NULL ? *s_page_data : (port_scanner_page_t){0};
  out_page->is_busy = s_is_page_busy;
  portEXIT_CRITICAL(&s_lock);
}

void port_scanner_close_page(void) {
  portENTER_CRITICAL(&s_lock);
  s_is_page_cancelled = true;
  portEXIT_CRITICAL(&s_lock);
}

esp_err_t port_scanner_start(const port_scanner_config_t *config) {
  esp_err_t error = port_scanner_validate(config);
  if (error != ESP_OK)
    return error;
  portENTER_CRITICAL(&s_lock);
  if (s_status.is_busy || s_is_page_busy) {
    portEXIT_CRITICAL(&s_lock);
    return ESP_ERR_INVALID_STATE;
  }
  s_status = (port_scanner_snapshot_t){
      .state = PORT_SCANNER_RUNNING, .is_busy = true, .is_ping_enabled = config->is_ping_enabled};
  s_is_stop_requested = false;
  s_result_first = 0;
  portEXIT_CRITICAL(&s_lock);
  port_scanner_job_t *job = heap_caps_calloc(1, sizeof(*job), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (job == NULL) {
    ESP_LOGE(TAG, "Scan allocation failed");
    goto cleanup;
  }
  if (s_results == NULL) {
    port_scan_result_t *results = heap_caps_calloc(
        PORT_SCANNER_RESULTS_MAX, sizeof(*results), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (results == NULL) {
      ESP_LOGE(TAG, "Result allocation failed");
      goto cleanup;
    }
    portENTER_CRITICAL(&s_lock);
    s_results = results;
    portEXIT_CRITICAL(&s_lock);
  }
  job->config = *config;
  // Loading common ports from flash requires a stack accessible with caches disabled.
  if (xTaskCreatePinnedToCoreWithCaps(worker,
                                      "port_scan",
                                      PORT_STACK_BYTES,
                                      job,
                                      SYS_PRIO_SERVICE_LO,
                                      NULL,
                                      SYS_CORE_RADIO,
                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) != pdPASS) {
    ESP_LOGE(TAG, "Scan worker allocation failed");
    goto cleanup;
  }
  return ESP_OK;
cleanup:
  free(job);
  portENTER_CRITICAL(&s_lock);
  s_status.is_busy = false;
  s_status.state = PORT_SCANNER_FAILED;
  s_status.error = ESP_ERR_NO_MEM;
  portEXIT_CRITICAL(&s_lock);
  led_signal_error();
  return ESP_ERR_NO_MEM;
}

static bool parse_number(const char **text, uint32_t limit, uint32_t *out_value) {
  const char *cursor = *text;
  if (!isdigit((unsigned char)*cursor))
    return false;
  uint32_t value = 0;
  do {
    uint32_t digit = (uint32_t)(*cursor++ - '0');
    if (value > (limit - digit) / DECIMAL_BASE)
      return false;
    value = value * DECIMAL_BASE + digit;
  } while (isdigit((unsigned char)*cursor));
  *text = cursor;
  *out_value = value;
  return true;
}

static bool parse_ip(const char *text, uint32_t *out_ip) {
  uint32_t address = 0;
  for (uint8_t i = 0; i < IPV4_BITS / OCTET_BITS; ++i) {
    uint32_t octet;
    if (!parse_number(&text, UINT8_MAX, &octet))
      return false;
    address = (address << OCTET_BITS) | octet;
    if (i + 1 < IPV4_BITS / OCTET_BITS && *text++ != '.')
      return false;
  }
  *out_ip = address;
  return *text == '\0';
}

static void cidr_bounds(uint32_t ip, uint32_t prefix, uint32_t *out_first, uint32_t *out_last) {
  uint32_t mask = prefix == 0 ? 0 : UINT32_MAX << (IPV4_BITS - prefix);
  *out_first = ip & mask;
  *out_last = *out_first | ~mask;
  if (prefix < IPV4_BITS - 1) {
    ++*out_first;
    --*out_last;
  }
}

static esp_err_t
target_bounds(const port_scanner_config_t *config, uint32_t *out_first, uint32_t *out_last) {
  char text[PORT_SCANNER_TARGET_LENGTH];
  memcpy(text, config->target, sizeof(text));
  text[sizeof(text) - 1] = '\0';
  char *separator = strchr(text, config->target_mode == PORT_SCANNER_CIDR ? '/' : '-');
  if (separator != NULL)
    *separator++ = '\0';
  if (!parse_ip(text, out_first))
    return ESP_ERR_INVALID_ARG;
  *out_last = *out_first;
  if (config->target_mode == PORT_SCANNER_RANGE) {
    if (separator == NULL || !parse_ip(separator, out_last) || *out_first > *out_last ||
        *out_last - *out_first == UINT32_MAX)
      return ESP_ERR_INVALID_ARG;
  } else if (config->target_mode == PORT_SCANNER_CIDR) {
    const char *prefix_text = separator;
    uint32_t prefix;
    if (prefix_text == NULL || !parse_number(&prefix_text, IPV4_BITS, &prefix) ||
        *prefix_text != '\0')
      return ESP_ERR_INVALID_ARG;
    cidr_bounds(*out_first, prefix, out_first, out_last);
  } else if (separator != NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  return ESP_OK;
}

static esp_err_t parse_ports(const char *text, uint8_t *bitmap) {
  bool has_port = false;
  while (*text != '\0') {
    while (isspace((unsigned char)*text))
      ++text;
    uint32_t first, last;
    if (!parse_number(&text, PORT_LIMIT, &first) || first == 0)
      return ESP_ERR_INVALID_ARG;
    last = first;
    if (*text == '-') {
      ++text;
      if (!parse_number(&text, PORT_LIMIT, &last) || last < first)
        return ESP_ERR_INVALID_ARG;
    }
    if (bitmap != NULL)
      for (uint32_t port = first; port <= last; ++port)
        bitmap[port / OCTET_BITS] |= (uint8_t)(1U << (port % OCTET_BITS));
    has_port = true;
    while (isspace((unsigned char)*text))
      ++text;
    if (*text == '\0')
      break;
    if (*text++ != ',' || *text == '\0')
      return ESP_ERR_INVALID_ARG;
  }
  return has_port ? ESP_OK : ESP_ERR_INVALID_ARG;
}

static bool stopped(void) {
  portENTER_CRITICAL(&s_lock);
  bool stop = s_is_stop_requested;
  portEXIT_CRITICAL(&s_lock);
  return stop;
}

static esp_err_t
rpc(spi_id_t id, const void *input, uint8_t size, void *output, size_t output_size) {
  spi_header_t header;
  esp_err_t error =
      spi_bridge_send_command(id, input, size, &header, output, output_size, PORT_RPC_MS);
  if (error == ESP_OK && output != NULL && header.length != output_size)
    return ESP_ERR_INVALID_RESPONSE;
  return error;
}

static esp_err_t
get_bounds(const port_scanner_config_t *config, uint32_t *out_first, uint32_t *out_last) {
  spi_wifi_ip_info_t info;
  uint8_t interface = 0;
  esp_err_t error =
      rpc(SPI_ID_WIFI_GET_IP_INFO, &interface, sizeof(interface), &info, sizeof(info));
  if (error != ESP_OK)
    return error;
  if (info.interface != interface)
    return ESP_ERR_INVALID_RESPONSE;
  if (info.ip == 0)
    return ESP_ERR_INVALID_STATE;
  if (config->target_mode == PORT_SCANNER_GATEWAY) {
    *out_first = *out_last = ntohl(info.gw);
    return info.gw != 0 ? ESP_OK : ESP_ERR_INVALID_STATE;
  }
  if (config->target_mode == PORT_SCANNER_LOCAL) {
    uint32_t mask = ntohl(info.netmask);
    uint32_t inverted = ~mask;
    if ((inverted & (inverted + 1)) != 0)
      return ESP_ERR_INVALID_RESPONSE;
    uint32_t ip = ntohl(info.ip);
    *out_first = ip & mask;
    *out_last = *out_first | inverted;
    if (inverted > 1) {
      ++*out_first;
      --*out_last;
    }
    return ESP_OK;
  }
  return target_bounds(config, out_first, out_last);
}

static void on_revoke(void *unused) {
  (void)unused;
  port_scanner_stop();
}

static esp_err_t wait_batch(esp_err_t start_error) {
  esp_err_t error = start_error;
  int64_t deadline = esp_timer_get_time() + PORT_BATCH_TIMEOUT_US;
  for (;;) {
    if (stopped() || error != ESP_OK) {
      portENTER_CRITICAL(&s_lock);
      s_status.state = PORT_SCANNER_STOPPING;
      s_status.error = error;
      portEXIT_CRITICAL(&s_lock);
      // Retain ownership until the C5 confirms the cancelled request is idle.
      (void)rpc(SPI_ID_WIFI_PORT_SCAN_STOP, NULL, 0, NULL, 0);
    }
    uint8_t busy = 1;
    esp_err_t poll = rpc(SPI_ID_WIFI_SCAN_STATUS, NULL, 0, &busy, sizeof(busy));
    if (poll == ESP_OK && busy == 0)
      return error;
    if (poll != ESP_OK)
      error = poll;
    else if (busy != 1)
      error = ESP_ERR_INVALID_RESPONSE;
    else if (esp_timer_get_time() >= deadline)
      error = ESP_ERR_TIMEOUT;
    vTaskDelay(pdMS_TO_TICKS(PORT_POLL_MS));
  }
}

static esp_err_t scan_batch(port_scanner_job_t *job, const char *ip, uint16_t count) {
  // Existing target-list wire request: IP[16], result limit, port count, ports.
  const uint16_t capacity = PORT_BATCH_SIZE;
  uint8_t *payload = job->payload;
  memcpy(payload, ip, PORT_SCANNER_IP_LENGTH);
  memcpy(payload + PORT_SCANNER_IP_LENGTH, &capacity, sizeof(capacity));
  memcpy(payload + PORT_COUNT_OFFSET, &count, sizeof(count));
  memcpy(payload + PORT_LIST_OFFSET, job->batch, count * sizeof(*job->batch));
  esp_err_t error = rpc(SPI_ID_WIFI_PORT_SCAN_TARGET_LIST,
                        payload,
                        PORT_LIST_OFFSET + count * sizeof(*job->batch),
                        NULL,
                        0);
  if (error == ESP_ERR_INVALID_STATE || error == ESP_ERR_INVALID_ARG ||
      error == ESP_ERR_NOT_SUPPORTED)
    return error;
  error = wait_batch(error);
  if (error != ESP_OK)
    return error;
  uint16_t index = SPI_DATA_INDEX_COUNT, total = 0;
  error = rpc(SPI_ID_SYSTEM_DATA, &index, sizeof(index), &total, sizeof(total));
  if (error != ESP_OK || total > count)
    return error != ESP_OK ? error : ESP_ERR_INVALID_RESPONSE;
  uint16_t received = 0;
  for (index = 0; index < total; ++index) {
    spi_port_scan_result_t record;
    error = rpc(SPI_ID_SYSTEM_DATA, &index, sizeof(index), &record, sizeof(record));
    if (error != ESP_OK)
      goto save_results;
    if (memcmp(record.ip_str, ip, sizeof(record.ip_str)) != 0 ||
        record.protocol != PORT_SCAN_PROTO_TCP || record.status != PORT_SCAN_STATUS_OPEN) {
      error = ESP_ERR_INVALID_RESPONSE;
      goto save_results;
    }
    bool has_port = false;
    for (uint16_t i = 0; i < count; ++i)
      has_port |= record.port == job->batch[i];
    if (!has_port) {
      error = ESP_ERR_INVALID_RESPONSE;
      goto save_results;
    }
    port_scan_result_t *result = &job->results[index];
    *result = (port_scan_result_t){
        .port = record.port, .protocol = PORT_SCAN_PROTO_TCP, .status = PORT_SCAN_STATUS_OPEN};
    memcpy(result->ip_str, record.ip_str, sizeof(result->ip_str));
    memcpy(result->banner, record.banner, sizeof(result->banner));
    result->banner[sizeof(result->banner) - 1] = '\0';
    ++received;
    portENTER_CRITICAL(&s_lock);
    if (s_status.found != UINT32_MAX)
      ++s_status.found;
    if (s_status.stored < PORT_SCANNER_RESULTS_MAX) {
      s_results[s_status.stored++] = *result;
    } else {
      s_results[s_result_first] = *result;
      s_result_first = (s_result_first + 1) % PORT_SCANNER_RESULTS_MAX;
    }
    portEXIT_CRITICAL(&s_lock);
  }
save_results:
  if (received != 0 && job->archive_path[0] != '\0') {
    esp_err_t storage_error = port_scanner_store_append(job->archive_path, job->results, received);
    portENTER_CRITICAL(&s_lock);
    s_status.storage_error = storage_error;
    if (storage_error == ESP_OK)
      s_status.saved += received;
    portEXIT_CRITICAL(&s_lock);
    if (storage_error != ESP_OK)
      return storage_error;
  }
  return error;
}

static esp_err_t probe_hosts(uint32_t first, uint8_t count, uint16_t *out_reachable) {
  *out_reachable = 0;
  const spi_wifi_host_probe_batch_req_t request = {.first_ip = htonl(first), .count = count};
  esp_err_t error = rpc(SPI_ID_WIFI_HOST_PROBE_BATCH_START, &request, sizeof(request), NULL, 0);
  if (error == ESP_ERR_INVALID_STATE || error == ESP_ERR_INVALID_ARG ||
      error == ESP_ERR_NOT_SUPPORTED)
    return error;
  error = wait_batch(error);
  if (error != ESP_OK)
    return error;
  spi_wifi_host_probe_batch_status_t result;
  error = rpc(SPI_ID_WIFI_HOST_PROBE_BATCH_STATUS, NULL, 0, &result, sizeof(result));
  if (error != ESP_OK)
    return error;
  uint16_t valid_mask = (1U << count) - 1U;
  if (result.is_running != 0 || result.first_ip != request.first_ip || result.count != count ||
      (result.reachable_mask & ~valid_mask) != 0)
    return ESP_ERR_INVALID_RESPONSE;
  *out_reachable = result.reachable_mask;
  if (stopped() && result.error == ESP_ERR_INVALID_STATE)
    return ESP_OK;
  return (esp_err_t)result.error;
}

static void format_host_ip(uint32_t host, char *out_ip) {
  snprintf(out_ip,
           PORT_SCANNER_IP_LENGTH,
           "%u.%u.%u.%u",
           (unsigned)(host >> (OCTET_BITS * 3)),
           (unsigned)((host >> (OCTET_BITS * 2)) & UINT8_MAX),
           (unsigned)((host >> OCTET_BITS) & UINT8_MAX),
           (unsigned)(host & UINT8_MAX));
}

static esp_err_t discover_hosts(port_scanner_job_t *job, uint32_t first, uint32_t hosts_total) {
  for (uint32_t offset = 0; offset < hosts_total && !stopped();) {
    uint8_t count = hosts_total - offset < SPI_WIFI_HOST_PROBE_BATCH_MAX
                        ? hosts_total - offset
                        : SPI_WIFI_HOST_PROBE_BATCH_MAX;
    char ip[PORT_SCANNER_IP_LENGTH] = {0};
    format_host_ip(first + offset, ip);
    portENTER_CRITICAL(&s_lock);
    memcpy(s_status.current_ip, ip, sizeof(ip));
    s_status.is_probing = true;
    s_status.probe_count = count;
    portEXIT_CRITICAL(&s_lock);
    uint16_t reachable = 0;
    esp_err_t error = probe_hosts(first + offset, count, &reachable);
    if (error != ESP_OK)
      return error;
    uint32_t responding = 0;
    for (uint8_t i = 0; i < count; ++i) {
      if ((reachable & (1U << i)) == 0)
        continue;
      uint32_t index = offset + i;
      job->reachable_hosts[index / OCTET_BITS] |= (uint8_t)(1U << (index % OCTET_BITS));
      ++responding;
    }
    portENTER_CRITICAL(&s_lock);
    s_status.hosts_responded += responding;
    if (!s_is_stop_requested) {
      s_status.hosts_probed += count;
      s_status.hosts_skipped += count - responding;
      s_status.hosts_done += count - responding;
    }
    portEXIT_CRITICAL(&s_lock);
    offset += count;
  }
  portENTER_CRITICAL(&s_lock);
  s_status.is_probing = false;
  s_status.probe_count = 0;
  s_status.is_discovery_complete = s_status.hosts_probed == hosts_total;
  s_status.current_ip[0] = '\0';
  portEXIT_CRITICAL(&s_lock);
  return ESP_OK;
}

static void worker(void *argument) {
  port_scanner_job_t *job = argument;
  res_handle_t radio = RES_HANDLE_NONE;
  bool has_power = false;
  res_request_t request = {.id = RES_WIFI, .owner_kind = RES_OWNER_UI, .on_revoke = on_revoke};
  esp_err_t error = resource_acquire(&request, &radio);
  if (error != ESP_OK)
    goto cleanup;
  error = power_manager_no_sleep_acquire();
  if (error != ESP_OK)
    goto cleanup;
  has_power = true;
  uint32_t first, last;
  error = get_bounds(&job->config, &first, &last);
  if (error != ESP_OK)
    goto cleanup;
  if (job->config.port_mode == PORT_SCANNER_COMMON_PORTS) {
    int ports[COMMON_PORTS_MAX];
    int count = common_ports_load(ports, COMMON_PORTS_MAX);
    for (int i = 0; i < count; ++i)
      if (ports[i] > 0 && ports[i] <= PORT_LIMIT)
        job->ports[ports[i] / OCTET_BITS] |= (uint8_t)(1U << (ports[i] % OCTET_BITS));
  } else {
    const char *ports =
        job->config.port_mode == PORT_SCANNER_ALL_PORTS ? "1-65535" : job->config.ports;
    error = parse_ports(ports, job->ports);
    if (error != ESP_OK)
      goto cleanup;
  }
  uint32_t ports_total = 0;
  for (uint32_t port = 1; port <= PORT_LIMIT; ++port)
    ports_total += (job->ports[port / OCTET_BITS] >> (port % OCTET_BITS)) & 1U;
  if (ports_total == 0) {
    error = ESP_ERR_INVALID_ARG;
    goto cleanup;
  }
  uint32_t hosts_total = last - first + 1;
  if (job->config.is_ping_enabled) {
    size_t bytes = hosts_total / OCTET_BITS + (hosts_total % OCTET_BITS != 0);
    job->reachable_hosts = heap_caps_calloc(bytes, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (job->reachable_hosts == NULL) {
      ESP_LOGE(TAG, "Cannot allocate host discovery bitmap (%lu bytes)", (unsigned long)bytes);
      error = ESP_ERR_NO_MEM;
      goto cleanup;
    }
  }
  esp_err_t storage_error =
      port_scanner_store_create(job->archive_path, sizeof(job->archive_path), first, last);
  portENTER_CRITICAL(&s_lock);
  s_status.storage_error = storage_error;
  memcpy(s_status.archive_path, job->archive_path, sizeof(s_status.archive_path));
  s_status.hosts_total = hosts_total;
  s_status.ports_total = ports_total;
  portEXIT_CRITICAL(&s_lock);
  if (storage_error != ESP_OK) {
    ESP_LOGW(TAG, "SD archive unavailable; retaining latest %u results", PORT_SCANNER_RESULTS_MAX);
    led_signal_warning();
  }
  ESP_LOGI(TAG,
           "Scan: %lu hosts, %lu ports each",
           (unsigned long)hosts_total,
           (unsigned long)ports_total);
  led_signal_info();
  if (job->config.is_ping_enabled) {
    error = discover_hosts(job, first, hosts_total);
    if (error != ESP_OK || stopped())
      goto cleanup;
  }
  for (uint32_t offset = 0; offset < hosts_total && !stopped(); ++offset) {
    if (offset != 0 && offset % HOST_YIELD_INTERVAL == 0)
      vTaskDelay(1);
    if (job->config.is_ping_enabled &&
        (job->reachable_hosts[offset / OCTET_BITS] & (1U << (offset % OCTET_BITS))) == 0)
      continue;
    char ip[PORT_SCANNER_IP_LENGTH] = {0};
    format_host_ip(first + offset, ip);
    portENTER_CRITICAL(&s_lock);
    memcpy(s_status.current_ip, ip, sizeof(ip));
    s_status.ports_done = 0;
    portEXIT_CRITICAL(&s_lock);
    uint16_t count = 0;
    for (uint32_t port = 1; port <= PORT_LIMIT && !stopped(); ++port) {
      if ((job->ports[port / OCTET_BITS] & (1U << (port % OCTET_BITS))) != 0)
        job->batch[count++] = (uint16_t)port;
      if (count != PORT_BATCH_SIZE && !(port == PORT_LIMIT && count != 0))
        continue;
      error = scan_batch(job, ip, count);
      if (error != ESP_OK)
        goto cleanup;
      portENTER_CRITICAL(&s_lock);
      if (!s_is_stop_requested)
        s_status.ports_done += count;
      portEXIT_CRITICAL(&s_lock);
      count = 0;
    }
    if (stopped())
      break;
    portENTER_CRITICAL(&s_lock);
    ++s_status.hosts_done;
    if (s_status.hosts_done < hosts_total)
      s_status.ports_done = 0;
    portEXIT_CRITICAL(&s_lock);
  }
cleanup:
  if (job->archive_path[0] != '\0' && error == ESP_OK) {
    ESP_LOGI(TAG, "Results saved: %s", job->archive_path);
    led_signal_info();
  }
  if (has_power)
    (void)power_manager_no_sleep_release();
  if (radio != RES_HANDLE_NONE)
    (void)resource_release(radio);
  free(job->reachable_hosts);
  free(job);
  portENTER_CRITICAL(&s_lock);
  s_status.error = error;
  s_status.is_probing = false;
  s_status.probe_count = 0;
  s_status.state = error != ESP_OK       ? PORT_SCANNER_FAILED
                   : s_is_stop_requested ? PORT_SCANNER_CANCELLED
                                         : PORT_SCANNER_DONE;
  s_status.is_busy = false;
  portEXIT_CRITICAL(&s_lock);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "Scan failed: %s", esp_err_to_name(error));
    led_signal_error();
  } else {
    ESP_LOGI(TAG, "Scan finished");
    led_signal_info();
  }
  vTaskDeleteWithCaps(NULL);
}

static bool page_cancelled(void) {
  portENTER_CRITICAL(&s_lock);
  bool is_cancelled = s_is_page_cancelled;
  portEXIT_CRITICAL(&s_lock);
  return is_cancelled;
}

static void page_worker(void *argument) {
  port_scanner_page_job_t *job = argument;
  esp_err_t error = power_manager_no_sleep_acquire();
  if (error == ESP_OK) {
    if (page_cancelled())
      error = ESP_ERR_INVALID_STATE;
    else if (job->request.kind == PORT_SCANNER_BROWSE_DELETE)
      error = port_scanner_store_delete(job->request.path);
    else
      error = port_scanner_store_browse(&job->request, &job->page, page_cancelled);
    (void)power_manager_no_sleep_release();
  }
  job->page.error = error;
  if (error != ESP_OK && !page_cancelled()) {
    ESP_LOGE(TAG, "Archive operation failed: %s", esp_err_to_name(error));
    led_signal_error();
  }
  if (error == ESP_OK && job->request.kind == PORT_SCANNER_BROWSE_DELETE) {
    portENTER_CRITICAL(&s_lock);
    if (strcmp(job->request.path, s_status.archive_path) == 0) {
      s_status.archive_path[0] = '\0';
      s_status.saved = 0;
      s_status.stored = 0;
    }
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "Scan deleted");
    led_signal_info();
  }
  portENTER_CRITICAL(&s_lock);
  if (!s_is_page_cancelled)
    *s_page_data = job->page;
  portEXIT_CRITICAL(&s_lock);
  free(job);
  portENTER_CRITICAL(&s_lock);
  s_is_page_busy = false;
  portEXIT_CRITICAL(&s_lock);
  vTaskDeleteWithCaps(NULL);
}
