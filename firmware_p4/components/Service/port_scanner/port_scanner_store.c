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

#include "port_scanner_store.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "esp_log.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "storage_atomic.h"
#include "storage_init.h"
#include "storage_mkdir.h"

#define RECORD_BYTES        160
#define IP_BYTES            15
#define PORT_OFFSET         16
#define PORT_DIGITS         5
#define TYPE_OFFSET         22
#define BANNER_OFFSET       31
#define BANNER_BYTES        64
#define HEX_RADIX           16
#define HEX_SHIFT           4
#define HEX_CHARS_PER_BYTE  2
#define IPV4_BITS           32
#define OCTET_BITS          8
#define TARGET_NAME_BYTES   26
#define TARGET_HEX_BYTES    8
#define TARGET_FIRST_OFFSET 5
#define TARGET_LAST_OFFSET  14
#define MAX_ARCHIVE_BYTES   INT32_MAX
#define DECIMAL_RADIX       10
#define EXTENSION_BYTES     4

typedef struct {
  FILE *file;
  uint32_t total;
  port_scanner_store_cancel_cb_t is_cancelled;
} port_scanner_reader_t;

static const char *TAG = "PORT_SCAN_STORE";
static const char HEADER[] = "ip,port,protocol,status,banner_hex\n";
static const char HEX[] = "0123456789abcdef";

_Static_assert(sizeof(((port_scan_result_t *)0)->banner) == BANNER_BYTES, "Archive banner size");
_Static_assert(sizeof(((port_scan_result_t *)0)->ip_str) == IP_BYTES + 1, "Archive IP size");

static void target_label(const char *name, char *out_label, size_t capacity);
static bool valid_name(const char *name);
static bool valid_path(const char *path);
static esp_err_t finish(FILE *file, esp_err_t error);
static esp_err_t inspect(FILE *file, uint32_t *out_count, bool *out_partial);
static esp_err_t decode(const char *line, port_scan_result_t *out_result);
static esp_err_t
read_record(port_scanner_reader_t *reader, uint32_t index, port_scan_result_t *out_result);
static esp_err_t
host_bound(port_scanner_reader_t *reader, const char *ip, bool is_upper, uint32_t *out_index);

esp_err_t
port_scanner_store_create(char *out_path, size_t capacity, uint32_t first, uint32_t last) {
  if (out_path == NULL || capacity < PORT_SCANNER_PATH_LENGTH || first > last)
    return ESP_ERR_INVALID_ARG;
  out_path[0] = '\0';
  if (!storage_is_mounted())
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = storage_mkdir_recursive(PORT_SCANNER_STORE_DIR);
  if (error != ESP_OK)
    return error;
  int length = snprintf(out_path,
                        capacity,
                        PORT_SCANNER_STORE_DIR "/scan_%08lx_%08lx.csv",
                        (unsigned long)first,
                        (unsigned long)last);
  if (length < 0 || (size_t)length >= capacity)
    error = ESP_ERR_INVALID_SIZE;
  else
    error = storage_write_atomic(out_path, HEADER, sizeof(HEADER) - 1);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "Cannot replace scan archive");
    out_path[0] = '\0';
  }
  return error;
}

esp_err_t
port_scanner_store_append(const char *path, const port_scan_result_t *results, uint16_t count) {
  if (!valid_path(path) || results == NULL || count == 0)
    return ESP_ERR_INVALID_ARG;
  FILE *file = fopen(path, "r+b");
  if (file == NULL)
    return ESP_FAIL;
  uint32_t existing = 0;
  bool is_partial = false;
  esp_err_t error = inspect(file, &existing, &is_partial);
  if (error != ESP_OK || is_partial ||
      existing > (MAX_ARCHIVE_BYTES - (sizeof(HEADER) - 1)) / RECORD_BYTES - count) {
    error = ESP_ERR_INVALID_SIZE;
    goto cleanup;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    error = ESP_FAIL;
    goto cleanup;
  }
  for (uint16_t i = 0; i < count; ++i) {
    const port_scan_result_t *result = &results[i];
    char line[RECORD_BYTES + 1];
    if (memchr(result->ip_str, '\0', sizeof(result->ip_str)) == NULL || result->port <= 0 ||
        result->port > UINT16_MAX ||
        (result->protocol != PORT_SCAN_PROTO_TCP && result->protocol != PORT_SCAN_PROTO_UDP) ||
        (result->status != PORT_SCAN_STATUS_OPEN &&
         (result->protocol != PORT_SCAN_PROTO_UDP ||
          (result->status != PORT_SCAN_STATUS_OPEN_FILTERED &&
           result->status != PORT_SCAN_STATUS_FILTERED)))) {
      error = ESP_ERR_INVALID_ARG;
      goto cleanup;
    }
    snprintf(line,
             sizeof(line),
             "%-15s,%05u,%s,%s,",
             result->ip_str,
             (unsigned)result->port,
             result->protocol == PORT_SCAN_PROTO_TCP ? "TCP" : "UDP",
             result->status == PORT_SCAN_STATUS_OPEN       ? "OPEN"
             : result->status == PORT_SCAN_STATUS_FILTERED ? "FILT"
                                                           : "OPFL");
    for (uint32_t j = 0; j < BANNER_BYTES; ++j) {
      uint8_t byte = (uint8_t)result->banner[j];
      line[BANNER_OFFSET + j * HEX_CHARS_PER_BYTE] = HEX[byte >> HEX_SHIFT];
      line[BANNER_OFFSET + j * HEX_CHARS_PER_BYTE + 1] = HEX[byte % HEX_RADIX];
    }
    line[RECORD_BYTES - 1] = '\n';
    if (fwrite(line, 1, RECORD_BYTES, file) != RECORD_BYTES) {
      error = ESP_FAIL;
      goto cleanup;
    }
  }
cleanup:
  return finish(file, error);
}

esp_err_t port_scanner_store_read(const char *path,
                                  port_scanner_page_t *out_page,
                                  port_scanner_store_cancel_cb_t is_cancelled) {
  if (!valid_path(path) || out_page == NULL || is_cancelled == NULL)
    return ESP_ERR_INVALID_ARG;
  FILE *file = fopen(path, "rb");
  if (file == NULL)
    return ESP_FAIL;
  esp_err_t error = inspect(file, &out_page->total, &out_page->is_partial);
  out_page->count = 0;
  if (error != ESP_OK)
    goto cleanup;
  uint64_t first = (uint64_t)out_page->number * PORT_SCANNER_PAGE_SIZE;
  if (first >= out_page->total)
    goto cleanup;
  long offset = (long)((sizeof(HEADER) - 1) + first * RECORD_BYTES);
  if (fseek(file, offset, SEEK_SET) != 0) {
    error = ESP_FAIL;
    goto cleanup;
  }
  while (out_page->count < PORT_SCANNER_PAGE_SIZE && first + out_page->count < out_page->total) {
    if (is_cancelled()) {
      error = ESP_ERR_INVALID_STATE;
      goto cleanup;
    }
    char line[RECORD_BYTES];
    if (fread(line, 1, sizeof(line), file) != sizeof(line)) {
      error = ESP_FAIL;
      goto cleanup;
    }
    error = decode(line, &out_page->results[out_page->count]);
    if (error != ESP_OK)
      goto cleanup;
    ++out_page->count;
  }
cleanup:
  if (fclose(file) != 0)
    error = ESP_FAIL;
  out_page->has_next = ((uint64_t)out_page->number + 1) * PORT_SCANNER_PAGE_SIZE < out_page->total;
  return error;
}

esp_err_t port_scanner_store_delete(const char *path) {
  if (!valid_path(path))
    return ESP_ERR_INVALID_ARG;
  if (!storage_is_mounted())
    return ESP_ERR_INVALID_STATE;
  return unlink(path) == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t port_scanner_store_browse(const port_scanner_browse_request_t *request,
                                    port_scanner_page_t *out_page,
                                    port_scanner_store_cancel_cb_t is_cancelled) {
  if (request == NULL || out_page == NULL || is_cancelled == NULL)
    return ESP_ERR_INVALID_ARG;
  out_page->number = request->number;
  if (request->kind == PORT_SCANNER_BROWSE_NETWORKS)
    return port_scanner_store_list(out_page, is_cancelled);
  if (request->kind == PORT_SCANNER_BROWSE_RECORDS)
    return port_scanner_store_read(request->path, out_page, is_cancelled);
  if (!valid_path(request->path) ||
      (request->kind != PORT_SCANNER_BROWSE_HOSTS && request->kind != PORT_SCANNER_BROWSE_PORTS))
    return ESP_ERR_INVALID_ARG;
  if (!storage_is_mounted())
    return ESP_ERR_INVALID_STATE;
  port_scanner_reader_t reader = {.file = fopen(request->path, "rb"), .is_cancelled = is_cancelled};
  if (reader.file == NULL)
    return ESP_FAIL;
  esp_err_t error = inspect(reader.file, &reader.total, &out_page->is_partial);
  out_page->count = 0;
  out_page->has_next = false;
  if (error != ESP_OK)
    goto cleanup;
  if (request->kind == PORT_SCANNER_BROWSE_PORTS) {
    if (request->host.first_record > reader.total ||
        request->host.count > reader.total - request->host.first_record) {
      error = ESP_ERR_INVALID_RESPONSE;
      goto cleanup;
    }
    out_page->total = request->host.count;
    uint64_t first = (uint64_t)request->number * PORT_SCANNER_PAGE_SIZE;
    while (out_page->count < PORT_SCANNER_PAGE_SIZE && first + out_page->count < out_page->total) {
      port_scan_result_t *result = &out_page->results[out_page->count];
      error = read_record(&reader, request->host.first_record + first + out_page->count, result);
      if (error != ESP_OK)
        goto cleanup;
      if (strcmp(result->ip_str, request->host.ip) != 0) {
        error = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
      }
      ++out_page->count;
    }
    out_page->has_next = first + out_page->count < out_page->total;
    goto cleanup;
  }
  uint32_t cursor = request->cursor;
  if (cursor > reader.total) {
    error = ESP_ERR_INVALID_ARG;
    goto cleanup;
  }
  // Results are written in host order; binary boundaries skip even 65535-port hosts.
  for (uint32_t i = 0; request->is_previous && i < PORT_SCANNER_PAGE_SIZE && cursor > 0; ++i) {
    port_scan_result_t result;
    uint32_t previous;
    error = read_record(&reader, cursor - 1, &result);
    if (error == ESP_OK)
      error = host_bound(&reader, result.ip_str, false, &previous);
    if (error != ESP_OK)
      goto cleanup;
    if (previous >= cursor) {
      error = ESP_ERR_INVALID_RESPONSE;
      goto cleanup;
    }
    cursor = previous;
  }
  out_page->cursor = cursor;
  out_page->total = reader.total;
  while (cursor < reader.total && out_page->count < PORT_SCANNER_PAGE_SIZE) {
    port_scan_result_t result;
    uint32_t next;
    error = read_record(&reader, cursor, &result);
    if (error == ESP_OK)
      error = host_bound(&reader, result.ip_str, true, &next);
    if (error != ESP_OK)
      goto cleanup;
    if (next <= cursor) {
      error = ESP_ERR_INVALID_RESPONSE;
      goto cleanup;
    }
    port_scanner_host_t *host = &out_page->hosts[out_page->count++];
    memcpy(host->ip, result.ip_str, sizeof(host->ip));
    host->first_record = cursor;
    host->count = next - cursor;
    cursor = next;
  }
  out_page->next_cursor = cursor;
  out_page->has_next = cursor < reader.total;
cleanup:
  if (fclose(reader.file) != 0)
    error = ESP_FAIL;
  return error;
}

esp_err_t port_scanner_store_list(port_scanner_page_t *out_page,
                                  port_scanner_store_cancel_cb_t is_cancelled) {
  if (out_page == NULL || is_cancelled == NULL)
    return ESP_ERR_INVALID_ARG;
  out_page->count = 0;
  out_page->total = 0;
  if (!storage_is_mounted())
    return ESP_ERR_INVALID_STATE;
  DIR *directory = opendir(PORT_SCANNER_STORE_DIR);
  if (directory == NULL)
    return errno == ENOENT ? ESP_OK : ESP_FAIL;
  esp_err_t error = ESP_OK;
  uint64_t first = (uint64_t)out_page->number * PORT_SCANNER_PAGE_SIZE;
  for (;;) {
    if (is_cancelled()) {
      error = ESP_ERR_INVALID_STATE;
      break;
    }
    errno = 0;
    struct dirent *entry = readdir(directory);
    if (entry == NULL) {
      if (errno != 0)
        error = ESP_FAIL;
      break;
    }
    if (!valid_name(entry->d_name))
      continue;
    if (out_page->total >= first && out_page->count < PORT_SCANNER_PAGE_SIZE) {
      memcpy(out_page->files[out_page->count], entry->d_name, strlen(entry->d_name) + 1);
      target_label(entry->d_name,
                   out_page->labels[out_page->count],
                   sizeof(out_page->labels[out_page->count]));
      ++out_page->count;
    }
    if (out_page->total == UINT32_MAX) {
      error = ESP_ERR_INVALID_SIZE;
      break;
    }
    ++out_page->total;
  }
  if (closedir(directory) != 0)
    error = ESP_FAIL;
  out_page->has_next = first + out_page->count < out_page->total;
  return error;
}

static void format_ip(uint32_t ip, char *out_text, size_t capacity) {
  snprintf(out_text,
           capacity,
           "%u.%u.%u.%u",
           (unsigned)(ip >> (OCTET_BITS * 3)),
           (unsigned)((ip >> (OCTET_BITS * 2)) & UINT8_MAX),
           (unsigned)((ip >> OCTET_BITS) & UINT8_MAX),
           (unsigned)(ip & UINT8_MAX));
}

static esp_err_t
read_record(port_scanner_reader_t *reader, uint32_t index, port_scan_result_t *out_result) {
  if (reader->is_cancelled())
    return ESP_ERR_INVALID_STATE;
  if (index >= reader->total)
    return ESP_ERR_INVALID_ARG;
  long offset = (long)(sizeof(HEADER) - 1 + (uint64_t)index * RECORD_BYTES);
  char line[RECORD_BYTES];
  if (fseek(reader->file, offset, SEEK_SET) != 0 ||
      fread(line, 1, sizeof(line), reader->file) != sizeof(line))
    return ESP_FAIL;
  return decode(line, out_result);
}

static esp_err_t
host_bound(port_scanner_reader_t *reader, const char *ip, bool is_upper, uint32_t *out_index) {
  struct in_addr target;
  if (inet_pton(AF_INET, ip, &target) != 1)
    return ESP_ERR_INVALID_ARG;
  uint32_t low = 0, high = reader->total;
  while (low < high) {
    uint32_t middle = low + (high - low) / 2;
    port_scan_result_t result;
    esp_err_t error = read_record(reader, middle, &result);
    if (error != ESP_OK)
      return error;
    struct in_addr address;
    if (inet_pton(AF_INET, result.ip_str, &address) != 1)
      return ESP_ERR_INVALID_RESPONSE;
    uint32_t value = ntohl(address.s_addr), key = ntohl(target.s_addr);
    if (value < key || (is_upper && value == key))
      low = middle + 1;
    else
      high = middle;
  }
  *out_index = low;
  return ESP_OK;
}

static void target_label(const char *name, char *out_label, size_t capacity) {
  if (strlen(name) != TARGET_NAME_BYTES || name[TARGET_LAST_OFFSET - 1] != '_') {
    snprintf(out_label, capacity, "Legacy %.*s", (int)(strlen(name) - EXTENSION_BYTES), name);
    return;
  }
  char hex[TARGET_HEX_BYTES + 1] = {0};
  memcpy(hex, name + TARGET_FIRST_OFFSET, TARGET_HEX_BYTES);
  uint32_t first = strtoul(hex, NULL, HEX_RADIX);
  memcpy(hex, name + TARGET_LAST_OFFSET, TARGET_HEX_BYTES);
  uint32_t last = strtoul(hex, NULL, HEX_RADIX);
  char first_text[PORT_SCANNER_IP_LENGTH], last_text[PORT_SCANNER_IP_LENGTH];
  format_ip(first, first_text, sizeof(first_text));
  if (first == last) {
    snprintf(out_label, capacity, "%s", first_text);
    return;
  }
  for (uint32_t prefix = 0; prefix < IPV4_BITS; ++prefix) {
    uint32_t mask = prefix == 0 ? 0 : UINT32_MAX << (IPV4_BITS - prefix);
    uint32_t network = first & mask, broadcast = network | ~mask;
    bool omits_edges = prefix < IPV4_BITS - 1;
    if (first == network + omits_edges && last == broadcast - omits_edges) {
      format_ip(network, first_text, sizeof(first_text));
      snprintf(out_label, capacity, "%s/%lu", first_text, (unsigned long)prefix);
      return;
    }
  }
  format_ip(last, last_text, sizeof(last_text));
  snprintf(out_label, capacity, "%s-%s", first_text, last_text);
}

static bool valid_name(const char *name) {
  size_t length = strlen(name);
  if (length >= PORT_SCANNER_NAME_LENGTH || length <= strlen("scan_.csv") ||
      strncmp(name, "scan_", strlen("scan_")) != 0 ||
      strcmp(name + length - EXTENSION_BYTES, ".csv") != 0)
    return false;
  for (size_t i = strlen("scan_"); i < length - EXTENSION_BYTES; ++i)
    if (!isxdigit((unsigned char)name[i]) && name[i] != '_')
      return false;
  return true;
}

static bool valid_path(const char *path) {
  const size_t prefix = sizeof(PORT_SCANNER_STORE_DIR "/") - 1;
  return path != NULL && strncmp(path, PORT_SCANNER_STORE_DIR "/", prefix) == 0 &&
         valid_name(path + prefix);
}

static esp_err_t finish(FILE *file, esp_err_t error) {
  if (fflush(file) != 0)
    error = ESP_FAIL;
  if (fclose(file) != 0)
    error = ESP_FAIL;
  return error;
}

static esp_err_t inspect(FILE *file, uint32_t *out_count, bool *out_partial) {
  char header[sizeof(HEADER) - 1];
  if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
      memcmp(header, HEADER, sizeof(header)) != 0)
    return ESP_ERR_INVALID_RESPONSE;
  if (fseek(file, 0, SEEK_END) != 0)
    return ESP_FAIL;
  long size = ftell(file);
  if (size < (long)sizeof(header) || size > MAX_ARCHIVE_BYTES)
    return ESP_ERR_INVALID_SIZE;
  *out_count = (size - sizeof(header)) / RECORD_BYTES;
  *out_partial = (size - sizeof(header)) % RECORD_BYTES != 0;
  return ESP_OK;
}

static esp_err_t decode(const char *line, port_scan_result_t *out_result) {
  bool is_tcp = memcmp(line + TYPE_OFFSET, "TCP,OPEN,", BANNER_OFFSET - TYPE_OFFSET) == 0;
  bool is_udp_open = memcmp(line + TYPE_OFFSET, "UDP,OPEN,", BANNER_OFFSET - TYPE_OFFSET) == 0;
  bool is_udp_unknown = memcmp(line + TYPE_OFFSET, "UDP,OPFL,", BANNER_OFFSET - TYPE_OFFSET) == 0;
  bool is_udp_filtered = memcmp(line + TYPE_OFFSET, "UDP,FILT,", BANNER_OFFSET - TYPE_OFFSET) == 0;
  if (line[IP_BYTES] != ',' || line[TYPE_OFFSET - 1] != ',' ||
      (!is_tcp && !is_udp_open && !is_udp_unknown && !is_udp_filtered) ||
      line[RECORD_BYTES - 1] != '\n')
    return ESP_ERR_INVALID_RESPONSE;
  *out_result = (port_scan_result_t){.protocol = is_tcp ? PORT_SCAN_PROTO_TCP : PORT_SCAN_PROTO_UDP,
                                     .status = is_udp_unknown    ? PORT_SCAN_STATUS_OPEN_FILTERED
                                               : is_udp_filtered ? PORT_SCAN_STATUS_FILTERED
                                                                 : PORT_SCAN_STATUS_OPEN};
  memcpy(out_result->ip_str, line, IP_BYTES);
  for (uint32_t i = IP_BYTES; i > 0 && out_result->ip_str[i - 1] == ' '; --i)
    out_result->ip_str[i - 1] = '\0';
  struct in_addr address;
  if (inet_pton(AF_INET, out_result->ip_str, &address) != 1)
    return ESP_ERR_INVALID_RESPONSE;
  uint32_t port = 0;
  for (uint32_t i = 0; i < PORT_DIGITS; ++i) {
    unsigned char digit = line[PORT_OFFSET + i];
    if (!isdigit(digit))
      return ESP_ERR_INVALID_RESPONSE;
    port = port * DECIMAL_RADIX + digit - '0';
  }
  if (port == 0 || port > UINT16_MAX)
    return ESP_ERR_INVALID_RESPONSE;
  out_result->port = port;
  for (uint32_t i = 0; i < BANNER_BYTES; ++i) {
    const char *high = memchr(HEX, line[BANNER_OFFSET + i * HEX_CHARS_PER_BYTE], HEX_RADIX);
    const char *low = memchr(HEX, line[BANNER_OFFSET + i * HEX_CHARS_PER_BYTE + 1], HEX_RADIX);
    if (high == NULL || low == NULL)
      return ESP_ERR_INVALID_RESPONSE;
    out_result->banner[i] = (char)(((high - HEX) << HEX_SHIFT) | (low - HEX));
  }
  out_result->banner[BANNER_BYTES - 1] = '\0';
  return ESP_OK;
}
