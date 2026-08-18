// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0

#include "lvgl_glue.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"

#include "st7789.h"
#include "sys_prio.h"

static const char *TAG = "LVGL_GLUE";

#define LVGL_PORT_TASK_PRIORITY   SYS_PRIO_RENDER
#define LVGL_PORT_TASK_STACK      (16 * 1024)
#define LVGL_PORT_MAX_SLEEP_MS    500
#define LVGL_PORT_TIMER_PERIOD_MS 5
#define LVGL_BUF_LINES            (LCD_PANEL_H / 4)
#define ROTATION_LOCK_TIMEOUT_MS  2000

static bool s_ready = false;
static bool s_landscape = false;
static lv_display_t *s_disp = NULL;
static volatile lvgl_glue_strip_cb_t s_capture_cb = NULL;

static SemaphoreHandle_t s_trans_done = NULL;
static volatile bool s_direct_mode = false;

static bool trans_done_cb(esp_lcd_panel_io_handle_t io,
                          esp_lcd_panel_io_event_data_t *ed, void *ctx) {
  (void)io;
  (void)ed;
  BaseType_t hp = pdFALSE;
  if (s_direct_mode) {
    if (s_trans_done != NULL)
      xSemaphoreGiveFromISR(s_trans_done, &hp);
  } else {
    lv_display_flush_ready((lv_display_t *)ctx);
  }
  return hp == pdTRUE;
}

static void capture_flush_start_cb(lv_event_t *e) {
  lvgl_glue_strip_cb_t cb = s_capture_cb;
  if (cb == NULL) {
    return;
  }
  lv_display_t *disp = (lv_display_t *)lv_event_get_target(e);
  const lv_area_t *area = (const lv_area_t *)lv_event_get_param(e);
  lv_draw_buf_t *buf = lv_display_get_buf_active(disp);
  if (area == NULL || buf == NULL || buf->data == NULL) {
    return;
  }
  cb(area->x1, area->y1, area->x2, area->y2, buf->data, (int32_t)buf->header.stride);
}

esp_err_t lvgl_glue_init(void) {
  if (s_ready) {
    return ESP_OK;
  }
  if (io_handle == NULL || panel_handle == NULL) {
    ESP_LOGE(TAG, "panel handles not ready — call st7789_init() first");
    return ESP_ERR_INVALID_STATE;
  }

  const lvgl_port_cfg_t port_cfg = {
      .task_priority = LVGL_PORT_TASK_PRIORITY,
      .task_stack = LVGL_PORT_TASK_STACK,
      .task_affinity = SYS_CORE_UI,
      .task_max_sleep_ms = LVGL_PORT_MAX_SLEEP_MS,
      .timer_period_ms = LVGL_PORT_TIMER_PERIOD_MS,
  };
  esp_err_t err = lvgl_port_init(&port_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "lvgl_port_init: %s", esp_err_to_name(err));
    return err;
  }

  const lvgl_port_display_cfg_t disp_cfg = {
      .io_handle = io_handle,
      .panel_handle = panel_handle,
      .buffer_size = LCD_PANEL_W * LVGL_BUF_LINES,
      .double_buffer = true,
      .hres = LCD_PANEL_W,
      .vres = LCD_PANEL_H,
      .monochrome = false,
      .rotation =
          {
              .swap_xy = false,
              .mirror_x = false,
              .mirror_y = false,
          },
      .flags =
          {
              .buff_dma = true,
              .buff_spiram = false,
              .swap_bytes = true,
          },
  };
  s_disp = lvgl_port_add_disp(&disp_cfg);
  if (s_disp == NULL) {
    ESP_LOGE(TAG, "lvgl_port_add_disp returned NULL");
    return ESP_FAIL;
  }
  lv_display_add_event_cb(s_disp, capture_flush_start_cb, LV_EVENT_FLUSH_START, NULL);

  s_trans_done = xSemaphoreCreateBinary();
  const esp_lcd_panel_io_callbacks_t io_cbs = {
      .on_color_trans_done = trans_done_cb,
  };
  esp_lcd_panel_io_register_event_callbacks(io_handle, &io_cbs, s_disp);

  ESP_LOGI(TAG,
           "LVGL up — %dx%d, partial double buffer (%d lines) in internal DMA RAM",
           LCD_PANEL_W,
           LCD_PANEL_H,
           LVGL_BUF_LINES);
  s_ready = true;
  return ESP_OK;
}

bool lvgl_glue_is_ready(void) {
  return s_ready;
}

void lvgl_glue_capture_begin(lvgl_glue_strip_cb_t cb) {
  s_capture_cb = cb;
}

void lvgl_glue_capture_end(void) {
  s_capture_cb = NULL;
}

void lvgl_glue_direct_begin(void) {
  if (s_trans_done != NULL)
    xSemaphoreTake(s_trans_done, 0);
  s_direct_mode = true;
}

void lvgl_glue_direct_end(void) {
  s_direct_mode = false;
}

void lvgl_glue_wait_flush(uint32_t timeout_ms) {
  if (s_trans_done != NULL)
    xSemaphoreTake(s_trans_done, pdMS_TO_TICKS(timeout_ms));
}

bool lvgl_glue_lock(int timeout_ms) {
  return lvgl_port_lock(timeout_ms);
}

void lvgl_glue_unlock(void) {
  lvgl_port_unlock();
}

bool lvgl_glue_toggle_rotation(void) {
  if (!s_ready || s_disp == NULL) {
    return s_landscape;
  }
  if (!lvgl_glue_lock(ROTATION_LOCK_TIMEOUT_MS)) {
    ESP_LOGW(TAG, "toggle_rotation: could not acquire LVGL lock");
    return s_landscape;
  }

  s_landscape = !s_landscape;
  lv_display_rotation_t r = s_landscape ? LV_DISPLAY_ROTATION_270 : LV_DISPLAY_ROTATION_0;
  lv_display_set_rotation(s_disp, r);
  lv_obj_invalidate(lv_screen_active());

  lvgl_glue_unlock();
  ESP_LOGI(TAG, "rotation: %s", s_landscape ? "landscape" : "portrait");
  return s_landscape;
}

bool lvgl_glue_is_landscape(void) {
  return s_landscape;
}
