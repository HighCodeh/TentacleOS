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

#include "lvgl_screen_share.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lvgl.h"

#include "host_link.h"
#include "lv_port_indev.h"
#include "lvgl_glue.h"
#include "spi_protocol.h"
#include "st7789.h"

static const char *TAG = "SCREEN_SHARE";

#define SS_FPS             12
#define SS_PERIOD_MS       (1000 / SS_FPS)
#define SS_WIDTH           LCD_PANEL_W // 240
#define SS_HEIGHT          LCD_PANEL_H // 320
#define SS_STRIP_ROWS      8           // 240*8*2 = 3840 B + header < 4067 host cap
#define SS_LOCK_TIMEOUT_MS 200
#define SS_TASK_STACK      8192
#define SS_TASK_PRIO       4

// Master enable. While false, START is rejected so the capture task never runs
// and no snapshot RAM is used.
static const bool s_enabled = true;

static volatile bool s_active = false;
static TaskHandle_t s_task = NULL;

// Reused snapshot target: our own DMA-incapable internal buffer wrapped in an
// lv_draw_buf (LVGL's mem pool is far too small for a 150 KB frame).
static lv_draw_buf_t s_snap;
static uint8_t *s_snap_data = NULL;
static uint32_t s_stride = 0;

// One row-strip on the wire: header + RGB565 pixels.
static uint8_t s_strip[sizeof(spi_screen_strip_t) + SS_STRIP_ROWS * SS_WIDTH * 2];

static void send_strips(void) {
  const uint8_t cat = SPI_CMD_CAT(SPI_ID_SCREEN_FRAME);
  const uint8_t op = SPI_CMD_OP(SPI_ID_SCREEN_FRAME);

  for (uint16_t y = 0; y < SS_HEIGHT; y += SS_STRIP_ROWS) {
    uint16_t rows = (y + SS_STRIP_ROWS <= SS_HEIGHT) ? SS_STRIP_ROWS : (uint16_t)(SS_HEIGHT - y);

    spi_screen_strip_t *hdr = (spi_screen_strip_t *)s_strip;
    hdr->y = y;
    hdr->rows = rows;
    hdr->width = SS_WIDTH;

    uint8_t *dst = s_strip + sizeof(spi_screen_strip_t);
    for (uint16_t r = 0; r < rows; r++) {
      memcpy(dst + (size_t)r * SS_WIDTH * 2, s_snap_data + (size_t)(y + r) * s_stride,
             (size_t)SS_WIDTH * 2);
    }

    uint16_t len = (uint16_t)(sizeof(spi_screen_strip_t) + (size_t)rows * SS_WIDTH * 2);
    host_link_emit_stream(cat, op, s_strip, len);
  }
}

static void capture_and_send(void) {
  // Snapshot must run under the LVGL lock (it walks the object tree). We render
  // into our private buffer, then release the lock before streaming (nothing
  // else touches s_snap_data, so the send needs no lock).
  if (!lvgl_glue_lock(SS_LOCK_TIMEOUT_MS)) {
    return;
  }
  bool ok = false;
  lv_obj_t *scr = lv_screen_active();
  if (scr != NULL && lv_obj_get_width(scr) == SS_WIDTH && lv_obj_get_height(scr) == SS_HEIGHT) {
    ok = (lv_snapshot_take_to_draw_buf(scr, LV_COLOR_FORMAT_RGB565, &s_snap) == LV_RESULT_OK);
  }
  lvgl_glue_unlock();

  if (ok) {
    send_strips();
  }
}

static void ss_task(void *arg) {
  (void)arg;

  s_stride = lv_draw_buf_width_to_stride(SS_WIDTH, LV_COLOR_FORMAT_RGB565);
  size_t data_size = (size_t)s_stride * SS_HEIGHT;
  s_snap_data = heap_caps_malloc(data_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (s_snap_data == NULL) {
    ESP_LOGE(TAG, "no memory for %u-byte snapshot buffer - screen share off", (unsigned)data_size);
    s_active = false;
    s_task = NULL;
    vTaskDelete(NULL);
    return;
  }
  lv_draw_buf_init(&s_snap, SS_WIDTH, SS_HEIGHT, LV_COLOR_FORMAT_RGB565, s_stride, s_snap_data,
                   data_size);

  ESP_LOGI(TAG, "screen share started (%dx%d @ %d fps, %u KB buffer)", SS_WIDTH, SS_HEIGHT, SS_FPS,
           (unsigned)(data_size / 1024));

  while (s_active) {
    capture_and_send();
    vTaskDelay(pdMS_TO_TICKS(SS_PERIOD_MS));
  }

  heap_caps_free(s_snap_data);
  s_snap_data = NULL;
  s_task = NULL;
  ESP_LOGI(TAG, "screen share stopped");
  vTaskDelete(NULL);
}

static void inject_key(uint8_t screen_key) {
  uint32_t lv_key;
  switch (screen_key) {
    case SPI_SCREEN_KEY_UP: lv_key = LV_KEY_UP; break;
    case SPI_SCREEN_KEY_DOWN: lv_key = LV_KEY_DOWN; break;
    case SPI_SCREEN_KEY_LEFT: lv_key = LV_KEY_LEFT; break;
    case SPI_SCREEN_KEY_RIGHT: lv_key = LV_KEY_RIGHT; break;
    case SPI_SCREEN_KEY_OK: lv_key = LV_KEY_ENTER; break;
    case SPI_SCREEN_KEY_BACK: lv_key = LV_KEY_ESC; break;
    default: return;
  }
  lv_port_indev_inject(lv_key);
}

bool lvgl_screen_share_is_host_op(uint16_t cmd) {
  return SPI_CMD_CAT(cmd) == SPI_CAT_SCREEN;
}

uint8_t lvgl_screen_share_handle(uint16_t cmd, const uint8_t *payload, uint16_t plen, uint8_t *out,
                                 size_t out_cap, uint16_t *out_len) {
  (void)out;
  (void)out_cap;
  if (out_len != NULL) {
    *out_len = 0;
  }

  switch (cmd) {
    case SPI_ID_SCREEN_START:
      if (!s_enabled) {
        return SPI_STATUS_UNSUPPORTED; // temporarily disabled
      }
      s_active = true;
      if (s_task == NULL) {
        if (xTaskCreate(ss_task, "screen_share", SS_TASK_STACK, NULL, SS_TASK_PRIO, &s_task) !=
            pdPASS) {
          s_active = false;
          s_task = NULL;
          return SPI_STATUS_ERROR;
        }
      }
      return SPI_STATUS_OK;

    case SPI_ID_SCREEN_STOP:
      lvgl_screen_share_stop();
      return SPI_STATUS_OK;

    case SPI_ID_SCREEN_KEY:
      if (payload == NULL || plen < 1) {
        return SPI_STATUS_INVALID_ARG;
      }
      inject_key(payload[0]);
      return SPI_STATUS_OK;

    default:
      return SPI_STATUS_UNSUPPORTED;
  }
}

void lvgl_screen_share_stop(void) {
  // Idempotent: the capture task sees s_active=false, frees its buffer and
  // exits on the next tick. Safe to call even when not streaming.
  s_active = false;
}

bool lvgl_screen_share_is_active(void) {
  return s_active;
}
