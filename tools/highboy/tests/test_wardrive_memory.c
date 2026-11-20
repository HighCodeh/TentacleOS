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


#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "wardrive_radio.c"
static bool fail_alloc, fail_start, busy;
static unsigned allocations, scans;
static uint16_t result_count, result_total;
static esp_err_t scan_error;
static void (*worker)(void);
void *heap_caps_calloc(size_t n, size_t z, unsigned caps) {
  allocations++;
  assert(n == SPI_WARD_MAX_RECORDS && z == sizeof(wifi_ap_record_t));
  assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  return fail_alloc ? NULL : calloc(n, z);
}
bool spi_bridge_async_scan_busy(void) { return busy; }
bool spi_bridge_async_scan_start(void (*fn)(void)) {
  if (fail_start) return false;
  worker = fn; busy = true; return true;
}
esp_err_t wifi_service_ward_scan(uint8_t band, wifi_ap_record_t *out,
                                 uint16_t *count, uint16_t *total) {
  assert(band == SPI_WIFI_BAND_5G); scans++;
  *count = scan_error ? 0 : result_count; *total = result_total;
  for (uint16_t i = 0; i < *count; i++) {
    memset(&out[i], 0, sizeof(out[i]));
    memset(out[i].ssid, 'A' + i % 26, sizeof(out[i].ssid));
    out[i].bssid[5] = i; out[i].primary = 36; out[i].authmode = 3; out[i].rssi = -42;
  }
  return scan_error;
}
static uint8_t reply[SPI_MAX_PAYLOAD], reply_len;
static spi_ward_status_t status(void) {
  assert(wardrive_radio_command(SPI_ID_WIFI_WARD_STATUS, NULL, 0, reply, &reply_len) == SPI_STATUS_OK);
  assert(reply_len == sizeof(spi_ward_status_t));
  spi_ward_status_t value; memcpy(&value, reply, sizeof(value)); return value;
}
static void start(void) {
  uint8_t band = SPI_WIFI_BAND_5G;
  assert(wardrive_radio_command(SPI_ID_WIFI_WARD_SCAN, &band, 1, reply, &reply_len) == SPI_STATUS_OK);
  assert(status().busy);
}
static void finish(void) { assert(worker); worker(); worker = NULL; busy = false; }
static spi_status_t record(uint32_t generation, uint16_t index) {
  spi_ward_request_t request = {.generation = generation, .index = index};
  return wardrive_radio_command(SPI_ID_WIFI_WARD_RECORD, (const uint8_t *)&request,
                                 sizeof(request), reply, &reply_len);
}
int main(void) {
  assert(record(0, 0) == SPI_STATUS_INVALID_ARG);
  fail_alloc = true; start(); finish();
  assert(status().error == ESP_ERR_NO_MEM && !status().busy && status().count == 0);
  assert(allocations == 1 && scans == 0);
  fail_alloc = false; result_count = SPI_WARD_MAX_RECORDS; result_total = 200;
  start(); uint32_t generation = status().generation;
  assert(record(generation, 0) == SPI_STATUS_INVALID_ARG);
  uint8_t band = SPI_WIFI_BAND_5G;
  assert(wardrive_radio_command(SPI_ID_WIFI_WARD_SCAN, &band, 1, reply, &reply_len) == SPI_STATUS_BUSY);
  finish();
  assert(status().count == SPI_WARD_MAX_RECORDS && status().total == 200 && !status().error);
  assert(allocations == 2 && scans == 1);
  assert(record(generation - 1, 0) == SPI_STATUS_INVALID_ARG);
  assert(record(generation, SPI_WARD_MAX_RECORDS) == SPI_STATUS_INVALID_ARG);
  assert(record(generation, 127) == SPI_STATUS_OK && reply_len == sizeof(spi_ward_record_t));
  spi_ward_record_t value; memcpy(&value, reply, sizeof(value));
  assert(value.bssid[5] == 127 && value.channel == 36 && value.authmode == 3 && value.rssi == -42);
  assert(value.ssid[31] == 'A' + 127 % 26 && value.ssid[32] == 0);
  result_count = result_total = 0; start(); finish();
  assert(!status().error && !status().count && allocations == 2);
  assert(record(generation, 0) == SPI_STATUS_INVALID_ARG);
  scan_error = ESP_ERR_INVALID_STATE; start(); finish();
  assert(status().error == scan_error && status().count == 0 && allocations == 2);
  fail_start = true;
  assert(wardrive_radio_command(SPI_ID_WIFI_WARD_SCAN, &band, 1, reply, &reply_len) == SPI_STATUS_ERROR);
  assert(!status().busy && status().error == ESP_ERR_NO_MEM && allocations == 2);
  free(s_records); s_records = NULL;
  return 0;
}
