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

// P4-local device state, settings (toggles), and raw console execution for the
// companion host link. Device state aggregates battery (BQ25896 gauge), the
// firmware versions (P4 local + C5 over SPI), and connection state. Console
// exec runs a line through esp_console and streams the captured stdout back as
// console-output LOG frames.

#include "host_link_state.h"

#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "esp_log.h"
#include "nvs.h"

#include "bq25896.h"
#include "host_link.h"
#include "ota_version.h"
#include "spi_bridge.h"
#include "spi_protocol.h"

static const char *TAG = "HOST_LINK_STATE";

#define HL_NVS_NAMESPACE     "hostlink"
#define HL_NVS_CONSOLE_KEY   "cons_exec"
#define HL_NVS_LOGBLE_KEY    "log_ble"
#define STATE_VERSION_MAX    32
#define STATE_CONSOLE_LINE   256
#define STATE_CONSOLE_CAPBUF 2048
#define STATE_CONSOLE_SPI_MS 3000

static bool s_console_exec_enabled = true;
static bool s_log_over_ble_enabled = true;

esp_err_t host_link_state_init(void) {
  nvs_handle_t h;
  if (nvs_open(HL_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
    return ESP_OK; // keep defaults if NVS unavailable
  }
  uint8_t v;
  if (nvs_get_u8(h, HL_NVS_CONSOLE_KEY, &v) == ESP_OK)
    s_console_exec_enabled = (v != 0);
  if (nvs_get_u8(h, HL_NVS_LOGBLE_KEY, &v) == ESP_OK)
    s_log_over_ble_enabled = (v != 0);
  nvs_close(h);
  return ESP_OK;
}

bool host_settings_console_exec_enabled(void) {
  return s_console_exec_enabled;
}

bool host_settings_log_over_ble_enabled(void) {
  return s_log_over_ble_enabled;
}

static void persist_settings(void) {
  nvs_handle_t h;
  if (nvs_open(HL_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK)
    return;
  nvs_set_u8(h, HL_NVS_CONSOLE_KEY, s_console_exec_enabled ? 1 : 0);
  nvs_set_u8(h, HL_NVS_LOGBLE_KEY, s_log_over_ble_enabled ? 1 : 0);
  nvs_commit(h);
  nvs_close(h);
}

// DeviceStatus = [battery_pct u8][charging u8][app_connected u8]
//                [ver_p4_len u8][ver_p4][ver_c5_len u8][ver_c5]
static uint8_t do_device_state(uint8_t *out, uint16_t cap, uint16_t *out_len) {
  uint16_t mv = bq25896_get_battery_voltage();
  uint8_t pct = (uint8_t)bq25896_get_battery_percentage(mv);
  uint8_t charging = bq25896_is_charging() ? 1 : 0;

  const char *ver_p4 = FIRMWARE_VERSION;
  size_t p4_full = strlen(ver_p4);
  uint8_t p4_len = (uint8_t)((p4_full < STATE_VERSION_MAX) ? p4_full : (STATE_VERSION_MAX - 1));

  char ver_c5[STATE_VERSION_MAX] = {0};
  uint8_t c5_len = 0;
  spi_header_t resp;
  uint8_t resp_buf[SPI_MAX_PAYLOAD];
  if (spi_bridge_send_command(
          SPI_ID_SYSTEM_VERSION, NULL, 0, &resp, resp_buf, sizeof(resp_buf), 1000) == ESP_OK) {
    c5_len = (resp.length < STATE_VERSION_MAX) ? resp.length : (STATE_VERSION_MAX - 1);
    memcpy(ver_c5, resp_buf, c5_len);
  }

  uint16_t need = (uint16_t)(3 + 1 + p4_len + 1 + c5_len);
  if (need > cap)
    return SPI_STATUS_ERROR;

  uint16_t off = 0;
  out[off++] = pct;
  out[off++] = charging;
  out[off++] = 1; // app_connected: replying implies a live companion session
  out[off++] = p4_len;
  memcpy(out + off, ver_p4, p4_len);
  off += p4_len;
  out[off++] = c5_len;
  memcpy(out + off, ver_c5, c5_len);
  off += c5_len;
  *out_len = off;
  return SPI_STATUS_OK;
}

static uint8_t do_console_exec(const uint8_t *payload, uint16_t plen) {
  if (!s_console_exec_enabled) {
    return SPI_STATUS_UNSUPPORTED;
  }
  if (payload == NULL || plen == 0 || plen >= STATE_CONSOLE_LINE) {
    return SPI_STATUS_INVALID_ARG;
  }

  char line[STATE_CONSOLE_LINE];
  memcpy(line, payload, plen);
  line[plen] = '\0';

  // Capture the command's stdout and forward it as console-output LOG frames.
  static char capbuf[STATE_CONSOLE_CAPBUF];
  FILE *mem = fmemopen(capbuf, sizeof(capbuf), "w");
  int cmd_ret = 0;
  if (mem != NULL) {
    FILE *saved = stdout;
    stdout = mem;
    esp_console_run(line, &cmd_ret);
    fflush(mem);
    stdout = saved;
    long n = ftell(mem);
    fclose(mem);
    for (long off = 0; off < n;) {
      long slice = n - off;
      if (slice > 200)
        slice = 200;
      host_link_emit_console(capbuf + off, (size_t)slice);
      off += slice;
    }
  } else {
    esp_console_run(line, &cmd_ret);
  }
  return SPI_STATUS_OK;
}

uint8_t host_state_handle(uint16_t cmd,
                          const uint8_t *payload,
                          uint16_t plen,
                          uint8_t *out_data,
                          uint16_t out_cap,
                          uint16_t *out_len) {
  *out_len = 0;

  switch (cmd) {
    case SPI_ID_SYSTEM_DEVICE_STATE:
      return do_device_state(out_data, out_cap, out_len);

    case SPI_ID_SYSTEM_CONSOLE_EXEC:
      return do_console_exec(payload, plen);

    case SPI_ID_SYSTEM_GET_SETTINGS:
      if (out_cap < 2)
        return SPI_STATUS_ERROR;
      out_data[0] = s_console_exec_enabled ? 1 : 0;
      out_data[1] = s_log_over_ble_enabled ? 1 : 0;
      *out_len = 2;
      return SPI_STATUS_OK;

    case SPI_ID_SYSTEM_SET_SETTINGS:
      if (payload == NULL || plen < 2)
        return SPI_STATUS_INVALID_ARG;
      s_console_exec_enabled = (payload[0] != 0);
      s_log_over_ble_enabled = (payload[1] != 0);
      persist_settings();
      if (out_cap >= 2) {
        out_data[0] = s_console_exec_enabled ? 1 : 0;
        out_data[1] = s_log_over_ble_enabled ? 1 : 0;
        *out_len = 2;
      }
      ESP_LOGI(TAG,
               "Settings: console_exec=%d log_over_ble=%d",
               s_console_exec_enabled,
               s_log_over_ble_enabled);
      return SPI_STATUS_OK;

    default:
      return SPI_STATUS_UNSUPPORTED;
  }
}
