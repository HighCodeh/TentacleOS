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

#include "host_link_badusb.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "bad_ble.h"
#include "bad_usb.h"
#include "ducky_parser.h"
#include "spi_protocol.h"

static const char *TAG = "HOST_BADUSB";

#define BADUSB_PATH_MAX      160
#define BADUSB_ASSETS_PREFIX "/assets/"
#define BADUSB_TASK_STACK    4096
#define BADUSB_TASK_PRIORITY SYS_PRIO_SERVICE_HI
#define BADUSB_TASK_CORE     SYS_CORE_RADIO

#define BADUSB_TARGET_USB 0x00
#define BADUSB_TARGET_BLE 0x01

static volatile bool s_busy = false;
static volatile bool s_abort = false;
static bool s_use_ble = false;
static TaskHandle_t s_task = NULL;
static char s_path[BADUSB_PATH_MAX];

static bool badusb_should_abort(void) {
  return s_abort;
}

static void run_current_path(void) {
  if (strncmp(s_path, BADUSB_ASSETS_PREFIX, strlen(BADUSB_ASSETS_PREFIX)) == 0)
    ducky_run_from_assets(s_path + strlen(BADUSB_ASSETS_PREFIX));
  else
    ducky_run_from_sdcard(s_path);
}

static void badusb_run_task(void *arg) {
  (void)arg;

  ducky_set_layout(DUCKY_LAYOUT_US);

  if (s_use_ble) {
    if (bad_ble_init() == ESP_OK) {
      ducky_set_output_mode(DUCKY_OUTPUT_BLUETOOTH);
      if (bad_ble_wait_for_connection_ex(badusb_should_abort) && !s_abort)
        run_current_path();
      bad_ble_deinit();
    }
  } else {
    bad_usb_init();
    ducky_set_output_mode(DUCKY_OUTPUT_USB);
    if (bad_usb_wait_for_connection_ex(badusb_should_abort) && !s_abort)
      run_current_path();
  }

  ESP_LOGI(TAG, "run finished: %s", s_path);
  s_busy = false;
  s_task = NULL;
  vTaskDelete(NULL);
}

uint8_t host_badusb_handle(uint16_t cmd,
                           const uint8_t *payload,
                           uint16_t plen,
                           uint8_t *out_data,
                           uint16_t out_cap,
                           uint16_t *out_len) {
  if (out_len != NULL)
    *out_len = 0;

  switch (SPI_CMD_OP(cmd)) {
    case SPI_CMD_OP(SPI_ID_BADUSB_RUN): {
      if (s_busy)
        return SPI_STATUS_BUSY;
      if (payload == NULL || plen == 0)
        return SPI_STATUS_INVALID_ARG;

      const uint8_t *path_bytes = payload;
      uint16_t path_plen = plen;
      s_use_ble = false;
      if (payload[0] <= BADUSB_TARGET_BLE) {
        s_use_ble = (payload[0] == BADUSB_TARGET_BLE);
        path_bytes = payload + 1;
        path_plen = (uint16_t)(plen - 1);
      }
      if (path_plen == 0)
        return SPI_STATUS_INVALID_ARG;

      uint16_t n = path_plen < sizeof(s_path) - 1 ? path_plen : (uint16_t)(sizeof(s_path) - 1);
      memcpy(s_path, path_bytes, n);
      s_path[n] = '\0';

      s_abort = false;
      s_busy = true;
      if (xTaskCreatePinnedToCore(badusb_run_task,
                                  "badusb_hl",
                                  BADUSB_TASK_STACK,
                                  NULL,
                                  BADUSB_TASK_PRIORITY,
                                  &s_task,
                                  BADUSB_TASK_CORE) != pdPASS) {
        s_busy = false;
        return SPI_STATUS_ERROR;
      }
      return SPI_STATUS_OK;
    }

    case SPI_CMD_OP(SPI_ID_BADUSB_ABORT):
      s_abort = true;
      ducky_abort();
      return SPI_STATUS_OK;

    case SPI_CMD_OP(SPI_ID_BADUSB_STATUS):
      if (out_data == NULL || out_cap < 1)
        return SPI_STATUS_ERROR;
      out_data[0] = s_busy ? 1 : 0;
      if (out_len != NULL)
        *out_len = 1;
      return SPI_STATUS_OK;

    default:
      return SPI_STATUS_UNSUPPORTED;
  }
}
