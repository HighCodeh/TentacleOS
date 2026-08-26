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

#include "host_link_theme.h"

#include <string.h>

#include "esp_log.h"

#include "spi_protocol.h"
#include "tos_config.h"
#include "tos_storage_paths.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "HOST_THEME";

#define THEME_NAME_MAX 32

static char s_theme_name[THEME_NAME_MAX];

static void apply_theme_cb(void *arg) {
  (void)arg;
  ui_theme_load_from_name(s_theme_name);
  ui_relayout_current_screen();
}

uint8_t host_theme_handle(uint16_t cmd,
                          const uint8_t *payload,
                          uint16_t plen,
                          uint8_t *out_data,
                          uint16_t out_cap,
                          uint16_t *out_len) {
  (void)cmd;
  (void)out_data;
  (void)out_cap;
  if (out_len != NULL)
    *out_len = 0;

  if (payload == NULL || plen == 0)
    return SPI_STATUS_INVALID_ARG;

  uint16_t n = plen < sizeof(s_theme_name) - 1 ? plen : (uint16_t)(sizeof(s_theme_name) - 1);
  memcpy(s_theme_name, payload, n);
  s_theme_name[n] = '\0';

  strlcpy(g_config_screen.theme, s_theme_name, sizeof(g_config_screen.theme));
  tos_config_save(TOS_PATH_CONFIG_SCREEN, "screen");

  ui_async_call(apply_theme_cb, NULL);
  ESP_LOGI(TAG, "theme set to '%s'", s_theme_name);
  return SPI_STATUS_OK;
}
