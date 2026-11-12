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

#include "console_service.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "gps_nmea.h"
#include "pin_def.h"

#define GPS_UART            UART_NUM_1
#define GPS_BAUD_DEFAULT    9600
#define GPS_BAUD_MIN        1200
#define GPS_BAUD_MAX        115200
#define GPS_TIMEOUT_DEFAULT 15
#define GPS_TIMEOUT_MAX     120
#define GPS_UART_BUFFER     1024
#define GPS_READ_MS         100
#define GPS_US_PER_SECOND   1000000LL

static const char *TAG = "GPS";
static atomic_bool s_is_busy;

static int cmd_gps(int argc, char **argv) {
  int baud = GPS_BAUD_DEFAULT;
  int seconds = GPS_TIMEOUT_DEFAULT;
  bool is_raw = false;
  bool is_info = false;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--geo")) {
      is_raw = false;
      is_info = false;
    } else if (!strcmp(argv[i], "--raw")) {
      is_raw = true;
    } else if (!strcmp(argv[i], "--info")) {
      is_info = true;
    } else if ((!strcmp(argv[i], "--baud") || !strcmp(argv[i], "--timeout")) && i + 1 < argc) {
      bool is_baud = !strcmp(argv[i], "--baud");
      char *end;
      long value = strtol(argv[++i], &end, 10);
      if (*end || value < (is_baud ? GPS_BAUD_MIN : 1) ||
          value > (is_baud ? GPS_BAUD_MAX : GPS_TIMEOUT_MAX))
        return 1;
      if (is_baud)
        baud = value;
      else
        seconds = value;
    } else {
      ESP_LOGI(TAG, "gps [--geo|--info|--raw] [--baud 9600] [--timeout 15] (max 120s)");
      return strcmp(argv[i], "--help") != 0;
    }
  }
  if (atomic_exchange(&s_is_busy, true)) {
    ESP_LOGW(TAG, "GPS command already running");
    return 1;
  }
  bool is_installed = false;
  bool has_result = false;
  unsigned bytes = 0;
  unsigned valid = 0;
  char line[GPS_NMEA_LINE_SIZE];
  size_t used = 0;
  esp_err_t err = ESP_ERR_INVALID_STATE;
  if (uart_is_driver_installed(GPS_UART))
    goto cleanup;
  err = uart_driver_install(GPS_UART, GPS_UART_BUFFER, 0, 0, NULL, 0);
  if (err != ESP_OK)
    goto cleanup;
  is_installed = true;
  uart_config_t config = {.baud_rate = baud,
                          .data_bits = UART_DATA_8_BITS,
                          .parity = UART_PARITY_DISABLE,
                          .stop_bits = UART_STOP_BITS_1,
                          .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                          .source_clk = UART_SCLK_DEFAULT};
  err = uart_param_config(GPS_UART, &config);
  if (err != ESP_OK)
    goto cleanup;
  err = uart_set_pin(
      GPS_UART, UART_PIN_NO_CHANGE, GPIO_EXT_GPS_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (err != ESP_OK)
    goto cleanup;
  err = uart_flush_input(GPS_UART);
  if (err != ESP_OK)
    goto cleanup;
  ESP_LOGI(TAG,
           "NMEA RX GPIO%d, %d baud, %ds; GPS TX -> header 13; GPS RX unconnected",
           GPIO_EXT_GPS_RX_PIN,
           baud,
           seconds);
  int64_t deadline = esp_timer_get_time() + seconds * GPS_US_PER_SECOND;
  while (esp_timer_get_time() < deadline) {
    uint8_t byte;
    int count = uart_read_bytes(GPS_UART, &byte, 1, pdMS_TO_TICKS(GPS_READ_MS));
    if (count < 0) {
      err = ESP_FAIL;
      goto cleanup;
    }
    if (!count)
      continue;
    bytes++;
    if (byte == '$') {
      used = 0;
      line[used++] = byte;
      continue;
    }
    if (!used || byte == '\r')
      continue;
    if (byte == '\n') {
      line[used] = 0;
      used = 0;
      if (is_raw) {
        ESP_LOGI(TAG, "%s", line);
        has_result = true;
      }
      gps_nmea_fix_t fix;
      int result = gps_nmea_parse(line, &fix);
      if (result < 0)
        continue;
      valid++;
      if (is_raw)
        continue;
      if (result || is_info) {
        ESP_LOGI(TAG,
                 "UTC=%s quality=%u satellites=%u HDOP=%s altitude_m=%s",
                 fix.utc,
                 fix.quality,
                 fix.satellites,
                 fix.hdop,
                 fix.altitude);
        if (result) {
          ESP_LOGI(TAG, "latitude=%.6f longitude=%.6f", fix.latitude, fix.longitude);
          has_result = true;
        } else {
          ESP_LOGW(TAG, "No position fix yet; keep antenna outdoors");
          has_result = is_info;
        }
        break;
      }
    } else if (used < sizeof(line) - 1 && byte >= ' ' && byte <= '~') {
      line[used++] = byte;
    } else
      used = 0;
  }
  if (!has_result)
    ESP_LOGW(TAG,
             "No valid position: bytes=%u GGA=%u (check power, baud, wiring and sky view)",
             bytes,
             valid);
cleanup:
  if (is_installed) {
    uart_driver_delete(GPS_UART);
    gpio_reset_pin(GPIO_EXT_GPS_RX_PIN);
  }
  atomic_store(&s_is_busy, false);
  if (err != ESP_OK)
    ESP_LOGE(TAG, "UART: %s", esp_err_to_name(err));
  return err == ESP_OK && has_result ? 0 : 1;
}

void register_gps_commands(void) {
  const esp_console_cmd_t command = {
      .command = "gps",
      .help = "External UART GPS: --geo, --info, --raw, --baud, --timeout",
      .func = cmd_gps};
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&command));
}
