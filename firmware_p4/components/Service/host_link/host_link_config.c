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

#include "host_link_config.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "audio_i2s.h"
#include "led_control.h"
#include "lvgl_glue.h"
#include "spi_protocol.h"
#include "st7789.h"
#include "tos_config.h"
#include "tos_storage_paths.h"
#include "ui_manager.h"

static const char *TAG = "HOST_CONFIG";

#define CFG_BODY_MAX       16
#define ROTATION_LANDSCAPE 2

typedef struct {
  uint8_t section;
  uint8_t body[CFG_BODY_MAX];
} cfg_job_t;

static void config_apply_cb(void *arg) {
  cfg_job_t *job = (cfg_job_t *)arg;
  switch (job->section) {
    case SPI_CFG_SECTION_DISPLAY: {
      spi_cfg_display_t d;
      memcpy(&d, job->body, sizeof(d));
      g_config_screen.brightness = d.brightness;
      g_config_screen.rotation = d.rotation;
      g_config_screen.auto_lock_seconds = d.auto_lock_seconds;
      g_config_screen.auto_dim = d.auto_dim != 0;
      tos_config_save(TOS_PATH_CONFIG_SCREEN, "screen");
      lcd_apply_brightness((uint8_t)g_config_screen.brightness);
      if ((g_config_screen.rotation == ROTATION_LANDSCAPE) != lvgl_glue_is_landscape())
        lvgl_glue_toggle_rotation();
      break;
    }
    case SPI_CFG_SECTION_SOUND: {
      spi_cfg_sound_t s;
      memcpy(&s, job->body, sizeof(s));
      g_config_system.volume = s.volume;
      g_config_system.vibration = s.vibration != 0;
      tos_config_save(TOS_PATH_CONFIG_SYSTEM, "system");
      audio_i2s_set_volume(s.volume);
      break;
    }
    case SPI_CFG_SECTION_CONNECTIVITY: {
      spi_cfg_connectivity_t c;
      memcpy(&c, job->body, sizeof(c));
      g_config_wifi.enabled = c.wifi_enabled != 0;
      g_config_ble.enabled = c.ble_enabled != 0;
      tos_config_save(TOS_PATH_CONFIG_WIFI, "wifi");
      tos_config_save(TOS_PATH_CONFIG_BLE, "ble");
      break;
    }
    case SPI_CFG_SECTION_LED: {
      spi_cfg_led_t l;
      memcpy(&l, job->body, sizeof(l));
      g_config_led.brightness = l.brightness;
      tos_config_save(TOS_PATH_CONFIG_LED, "led");
      led_set_signal_config(g_config_led.info_color,
                            g_config_led.warning_color,
                            g_config_led.error_color,
                            g_config_led.brightness);
      break;
    }
    default:
      break;
  }

  free(job);
}

static uint16_t section_size(uint8_t section) {
  switch (section) {
    case SPI_CFG_SECTION_DISPLAY:
      return sizeof(spi_cfg_display_t);
    case SPI_CFG_SECTION_SOUND:
      return sizeof(spi_cfg_sound_t);
    case SPI_CFG_SECTION_CONNECTIVITY:
      return sizeof(spi_cfg_connectivity_t);
    case SPI_CFG_SECTION_LED:
      return sizeof(spi_cfg_led_t);
    default:
      return 0;
  }
}

static uint8_t do_get(uint8_t section, uint8_t *out, uint16_t out_cap, uint16_t *out_len) {
  switch (section) {
    case SPI_CFG_SECTION_DISPLAY: {
      if (out_cap < sizeof(spi_cfg_display_t))
        return SPI_STATUS_ERROR;
      spi_cfg_display_t d = {
          .brightness = (uint8_t)g_config_screen.brightness,
          .rotation = (uint8_t)g_config_screen.rotation,
          .auto_lock_seconds = (uint16_t)g_config_screen.auto_lock_seconds,
          .auto_dim = g_config_screen.auto_dim ? 1 : 0,
      };
      memcpy(out, &d, sizeof(d));
      *out_len = sizeof(d);
      return SPI_STATUS_OK;
    }
    case SPI_CFG_SECTION_SOUND: {
      if (out_cap < sizeof(spi_cfg_sound_t))
        return SPI_STATUS_ERROR;
      spi_cfg_sound_t s = {
          .volume = (uint8_t)g_config_system.volume,
          .vibration = g_config_system.vibration ? 1 : 0,
      };
      memcpy(out, &s, sizeof(s));
      *out_len = sizeof(s);
      return SPI_STATUS_OK;
    }
    case SPI_CFG_SECTION_CONNECTIVITY: {
      if (out_cap < sizeof(spi_cfg_connectivity_t))
        return SPI_STATUS_ERROR;
      spi_cfg_connectivity_t c = {
          .wifi_enabled = g_config_wifi.enabled ? 1 : 0,
          .ble_enabled = g_config_ble.enabled ? 1 : 0,
      };
      memcpy(out, &c, sizeof(c));
      *out_len = sizeof(c);
      return SPI_STATUS_OK;
    }
    case SPI_CFG_SECTION_LED: {
      if (out_cap < sizeof(spi_cfg_led_t))
        return SPI_STATUS_ERROR;
      spi_cfg_led_t l = {.brightness = (uint8_t)g_config_led.brightness};
      memcpy(out, &l, sizeof(l));
      *out_len = sizeof(l);
      return SPI_STATUS_OK;
    }
    default:
      return SPI_STATUS_INVALID_ARG;
  }
}

static uint8_t do_set(uint8_t section, const uint8_t *body, uint16_t blen) {
  uint16_t need = section_size(section);
  if (need == 0 || blen < need || need > CFG_BODY_MAX)
    return SPI_STATUS_INVALID_ARG;

  cfg_job_t *job = malloc(sizeof(cfg_job_t));
  if (job == NULL) {
    ESP_LOGE(TAG, "config job alloc failed");
    return SPI_STATUS_ERROR;
  }
  job->section = section;
  memset(job->body, 0, sizeof(job->body));
  memcpy(job->body, body, need);

  ui_async_call(config_apply_cb, job);
  return SPI_STATUS_OK;
}

bool host_config_is_op(uint16_t cmd) {
  return cmd == SPI_ID_SYSTEM_CONFIG_GET || cmd == SPI_ID_SYSTEM_CONFIG_SET;
}

uint8_t host_config_handle(uint16_t cmd,
                           const uint8_t *payload,
                           uint16_t plen,
                           uint8_t *out_data,
                           uint16_t out_cap,
                           uint16_t *out_len) {
  if (out_len != NULL)
    *out_len = 0;
  if (payload == NULL || plen < 1)
    return SPI_STATUS_INVALID_ARG;

  if (cmd == SPI_ID_SYSTEM_CONFIG_GET)
    return do_get(payload[0], out_data, out_cap, out_len);
  if (cmd == SPI_ID_SYSTEM_CONFIG_SET)
    return do_set(payload[0], payload + 1, (uint16_t)(plen - 1));
  return SPI_STATUS_UNSUPPORTED;
}
