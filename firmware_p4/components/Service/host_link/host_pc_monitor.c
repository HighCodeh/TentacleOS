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

#include "host_pc_monitor.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "lwip/sockets.h"

#include "host_link.h"
#include "spi_protocol.h"

#define CPU_OFFSET        2
#define RAM_OFFSET        4
#define DISK_OFFSET       6
#define RAM_USED_OFFSET   8
#define RAM_TOTAL_OFFSET  12
#define DISK_USED_OFFSET  16
#define DISK_TOTAL_OFFSET 20
#define HOSTNAME_OFFSET   24
#define VOLUME_OFFSET     56
#define LOCAL_IP_OFFSET   88
#define PUBLIC_IP_OFFSET  134
#define BYTE_BITS         8
#define WORD_BYTES        4
#define ASCII_MIN         32
#define ASCII_MAX         126

static const char *TAG = "HOST_PC_MONITOR";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static host_pc_monitor_snapshot_t s_snapshot;
static uint32_t s_generation;
static int64_t s_received_us;

static uint16_t read_u16(const uint8_t *data);
static uint32_t read_u32(const uint8_t *data);
static bool copy_text(char *out, const uint8_t *data, size_t size);
static bool valid_ip(const char *ip);

bool host_pc_monitor_get(host_pc_monitor_snapshot_t *out_snapshot) {
  if (out_snapshot == NULL)
    return false;
  uint32_t generation = host_link_output_generation();
  int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&s_lock);
  bool is_fresh = generation != 0 && generation == s_generation && now >= s_received_us &&
                  now - s_received_us < HOST_PC_MONITOR_STALE_US;
  if (is_fresh)
    *out_snapshot = s_snapshot;
  else
    memset(out_snapshot, 0, sizeof(*out_snapshot));
  portEXIT_CRITICAL(&s_lock);
  return is_fresh;
}

uint8_t host_pc_monitor_handle(uint16_t cmd,
                               const uint8_t *payload,
                               uint16_t plen,
                               uint8_t *out,
                               uint16_t out_cap,
                               uint16_t *out_len) {
  (void)out;
  (void)out_cap;
  if (out_len == NULL)
    return SPI_STATUS_INVALID_ARG;
  *out_len = 0;
  if (cmd != SPI_ID_SYSTEM_PC_MONITOR)
    return SPI_STATUS_UNSUPPORTED;
  if (payload == NULL || plen != HOST_PC_MONITOR_WIRE_SIZE ||
      payload[0] != HOST_PC_MONITOR_VERSION || payload[1] != 0)
    return SPI_STATUS_INVALID_ARG;
  host_pc_monitor_snapshot_t sample = {0};
  sample.cpu = read_u16(payload + CPU_OFFSET);
  sample.ram = read_u16(payload + RAM_OFFSET);
  sample.disk = read_u16(payload + DISK_OFFSET);
  sample.ram_used_mib = read_u32(payload + RAM_USED_OFFSET);
  sample.ram_total_mib = read_u32(payload + RAM_TOTAL_OFFSET);
  sample.disk_used_mib = read_u32(payload + DISK_USED_OFFSET);
  sample.disk_total_mib = read_u32(payload + DISK_TOTAL_OFFSET);
  if (sample.cpu > HOST_PC_MONITOR_PERCENT_MAX || sample.ram > HOST_PC_MONITOR_PERCENT_MAX ||
      sample.disk > HOST_PC_MONITOR_PERCENT_MAX || sample.ram_total_mib == 0 ||
      sample.disk_total_mib == 0 || sample.ram_used_mib > sample.ram_total_mib ||
      sample.disk_used_mib > sample.disk_total_mib ||
      !copy_text(sample.hostname, payload + HOSTNAME_OFFSET, sizeof(sample.hostname)) ||
      !copy_text(sample.volume, payload + VOLUME_OFFSET, sizeof(sample.volume)) ||
      !copy_text(sample.local_ip, payload + LOCAL_IP_OFFSET, sizeof(sample.local_ip)) ||
      !copy_text(sample.public_ip, payload + PUBLIC_IP_OFFSET, sizeof(sample.public_ip)) ||
      sample.hostname[0] == '\0' || sample.volume[0] == '\0' || !valid_ip(sample.local_ip) ||
      !valid_ip(sample.public_ip))
    return SPI_STATUS_INVALID_ARG;
  uint32_t generation = host_link_output_generation();
  if (generation == 0)
    return SPI_STATUS_ERROR;
  int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&s_lock);
  s_snapshot = sample;
  s_generation = generation;
  s_received_us = now;
  portEXIT_CRITICAL(&s_lock);
  ESP_LOGD(TAG, "PC sample received");
  return SPI_STATUS_OK;
}

static uint16_t read_u16(const uint8_t *data) {
  return (uint16_t)data[0] | ((uint16_t)data[1] << BYTE_BITS);
}

static uint32_t read_u32(const uint8_t *data) {
  uint32_t value = 0;
  for (size_t i = 0; i < WORD_BYTES; i++)
    value |= (uint32_t)data[i] << (i * BYTE_BITS);
  return value;
}

static bool copy_text(char *out, const uint8_t *data, size_t size) {
  const uint8_t *end = memchr(data, 0, size);
  if (end == NULL)
    return false;
  for (const uint8_t *p = data; p < end; p++) {
    if (*p < ASCII_MIN || *p > ASCII_MAX)
      return false;
  }
  memcpy(out, data, (size_t)(end - data));
  return true;
}

static bool valid_ip(const char *ip) {
  if (ip[0] == '\0')
    return true;
  struct in_addr ipv4;
  struct in6_addr ipv6;
  return inet_pton(AF_INET, ip, &ipv4) == 1 || inet_pton(AF_INET6, ip, &ipv6) == 1;
}
