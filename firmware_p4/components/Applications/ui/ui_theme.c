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

#include "ui_theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "assets_manager.h"
#include "cJSON.h"
#include "storage_assets.h"
#include "tos_config.h"

#include "tos_flash_paths.h"
#define THEME_CONFIG_PATH     FLASH_CONFIG_THEMES
#define INTERFACE_CONFIG_PATH FLASH_CONFIG_INTERFACE

#define THEME_COUNT           2
#define CONF_KV_BUF_SIZE      64
#define THEME_PATH_MAX        128
#define ASSET_DIR_PATH_MAX    160
#define CONF_SECTION_NONE     0
#define CONF_SECTION_COLORS   1
#define CONF_SECTION_PROTOCOL 2

static const char *TAG = "UI_THEME";

ui_theme_t current_theme;
int theme_idx = 0;
static protocol_id_t active_protocol = PROTOCOL_NONE;

const char *theme_names[] = {"default", "cyber_blue"};

static uint32_t hex_to_int(const char *hex) {
  if (hex == NULL)
    return 0;
  return (uint32_t)strtol(hex, NULL, 16);
}

void ui_theme_load_settings(void) {
  if (!storage_assets_is_mounted())
    return;

  FILE *f = fopen(INTERFACE_CONFIG_PATH, "r");
  if (f == NULL)
    return;

  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *data = malloc(fsize + 1);

  if (data) {
    fread(data, 1, fsize, f);
    data[fsize] = 0;
    cJSON *root = cJSON_Parse(data);
    if (root) {
      cJSON *theme = cJSON_GetObjectItem(root, "theme");
      if (cJSON_IsString(theme)) {
        for (int i = 0; i < THEME_COUNT; i++) {
          if (strcmp(theme->valuestring, theme_names[i]) == 0) {
            theme_idx = i;
            break;
          }
        }
      }
      cJSON_Delete(root);
    }
    free(data);
  }
  fclose(f);
}

void ui_theme_load_idx(int color_idx) {
  if (color_idx < 0 || color_idx > (THEME_COUNT - 1))
    color_idx = 0;

  theme_idx = color_idx;

  FILE *f = fopen(THEME_CONFIG_PATH, "r");
  if (f == NULL) {
    ESP_LOGW(TAG, "Theme file not found, using built-in default palette");
    current_theme.bg_primary = lv_color_hex(0x000000);
    current_theme.bg_secondary = lv_color_hex(0x0A0014);
    current_theme.bg_item_top = lv_color_hex(0x0A0014);
    current_theme.bg_item_bot = lv_color_hex(0x0A0014);
    current_theme.border_accent = lv_color_hex(0xCC00FF);
    current_theme.border_interface = lv_color_hex(0xBF00FF);
    current_theme.border_inactive = lv_color_hex(0x2A2A2A);
    current_theme.text_main = lv_color_hex(0xFFFFFF);
    current_theme.text_secondary = lv_color_hex(0x8A8594);
    current_theme.screen_base = lv_color_hex(0x000000);
    current_theme.protocol_nfc = lv_color_hex(0xCC00FF);
    current_theme.protocol_wifi = lv_color_hex(0xBF00FF);
    current_theme.protocol_ble = lv_color_hex(0xD158F2);
    current_theme.protocol_subghz = lv_color_hex(0x4CAF50);
    current_theme.protocol_rfid = lv_color_hex(0xFFC107);
    current_theme.protocol_ir = lv_color_hex(0xF44336);
    current_theme.protocol_lora = lv_color_hex(0x00BCD4);
    return;
  }

  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);

  char *data = malloc(fsize + 1);
  if (data == NULL) {
    fclose(f);
    return;
  }

  fread(data, 1, fsize, f);
  data[fsize] = 0;
  fclose(f);

  cJSON *root = cJSON_Parse(data);
  if (root) {
    cJSON *theme_node = cJSON_GetObjectItem(root, theme_names[color_idx]);
    if (theme_node) {
      current_theme.bg_primary =
          lv_color_hex(hex_to_int(cJSON_GetObjectItem(theme_node, "bg_primary")->valuestring));
      current_theme.bg_secondary =
          lv_color_hex(hex_to_int(cJSON_GetObjectItem(theme_node, "bg_secondary")->valuestring));
      current_theme.bg_item_top =
          lv_color_hex(hex_to_int(cJSON_GetObjectItem(theme_node, "bg_item_top")->valuestring));
      current_theme.bg_item_bot =
          lv_color_hex(hex_to_int(cJSON_GetObjectItem(theme_node, "bg_item_bot")->valuestring));
      current_theme.border_accent =
          lv_color_hex(hex_to_int(cJSON_GetObjectItem(theme_node, "border_accent")->valuestring));
      current_theme.border_interface = lv_color_hex(
          hex_to_int(cJSON_GetObjectItem(theme_node, "border_interface")->valuestring));
      current_theme.border_inactive =
          lv_color_hex(hex_to_int(cJSON_GetObjectItem(theme_node, "border_inactive")->valuestring));
      current_theme.text_main =
          lv_color_hex(hex_to_int(cJSON_GetObjectItem(theme_node, "text_main")->valuestring));
      current_theme.screen_base =
          lv_color_hex(hex_to_int(cJSON_GetObjectItem(theme_node, "screen_base")->valuestring));

      cJSON *v;
      v = cJSON_GetObjectItem(theme_node, "text_secondary");
      current_theme.text_secondary =
          v ? lv_color_hex(hex_to_int(v->valuestring)) : lv_color_hex(0x8A8594);
      v = cJSON_GetObjectItem(theme_node, "protocol_nfc");
      current_theme.protocol_nfc =
          v ? lv_color_hex(hex_to_int(v->valuestring)) : lv_color_hex(0x2196F3);
      v = cJSON_GetObjectItem(theme_node, "protocol_wifi");
      current_theme.protocol_wifi =
          v ? lv_color_hex(hex_to_int(v->valuestring)) : lv_color_hex(0x834EC6);
      v = cJSON_GetObjectItem(theme_node, "protocol_ble");
      current_theme.protocol_ble =
          v ? lv_color_hex(hex_to_int(v->valuestring)) : lv_color_hex(0x0082FC);
      v = cJSON_GetObjectItem(theme_node, "protocol_subghz");
      current_theme.protocol_subghz =
          v ? lv_color_hex(hex_to_int(v->valuestring)) : lv_color_hex(0x4CAF50);
      v = cJSON_GetObjectItem(theme_node, "protocol_rfid");
      current_theme.protocol_rfid =
          v ? lv_color_hex(hex_to_int(v->valuestring)) : lv_color_hex(0xFFC107);
      v = cJSON_GetObjectItem(theme_node, "protocol_ir");
      current_theme.protocol_ir =
          v ? lv_color_hex(hex_to_int(v->valuestring)) : lv_color_hex(0xF44336);
      v = cJSON_GetObjectItem(theme_node, "protocol_lora");
      current_theme.protocol_lora =
          v ? lv_color_hex(hex_to_int(v->valuestring)) : lv_color_hex(0x00BCD4);
    }
    cJSON_Delete(root);
  }
  free(data);
}

static void apply_conf_color(const char *key, const char *value, int section) {
  uint32_t hex = (uint32_t)strtol(value, NULL, 16);
  lv_color_t color = lv_color_hex(hex);

  if (section == CONF_SECTION_COLORS) {
    if (strcmp(key, "bg_primary") == 0)
      current_theme.bg_primary = color;
    else if (strcmp(key, "bg_secondary") == 0)
      current_theme.bg_secondary = color;
    else if (strcmp(key, "bg_item_top") == 0)
      current_theme.bg_item_top = color;
    else if (strcmp(key, "bg_item_bot") == 0)
      current_theme.bg_item_bot = color;
    else if (strcmp(key, "border_accent") == 0)
      current_theme.border_accent = color;
    else if (strcmp(key, "border_interface") == 0)
      current_theme.border_interface = color;
    else if (strcmp(key, "border_inactive") == 0)
      current_theme.border_inactive = color;
    else if (strcmp(key, "text_main") == 0)
      current_theme.text_main = color;
    else if (strcmp(key, "text_secondary") == 0)
      current_theme.text_secondary = color;
    else if (strcmp(key, "screen_base") == 0)
      current_theme.screen_base = color;
  } else if (section == CONF_SECTION_PROTOCOL) {
    if (strcmp(key, "nfc") == 0)
      current_theme.protocol_nfc = color;
    else if (strcmp(key, "wifi") == 0)
      current_theme.protocol_wifi = color;
    else if (strcmp(key, "ble") == 0)
      current_theme.protocol_ble = color;
    else if (strcmp(key, "subghz") == 0)
      current_theme.protocol_subghz = color;
    else if (strcmp(key, "rfid") == 0)
      current_theme.protocol_rfid = color;
    else if (strcmp(key, "ir") == 0)
      current_theme.protocol_ir = color;
    else if (strcmp(key, "lora") == 0)
      current_theme.protocol_lora = color;
  }
}

static void parse_theme_conf(const char *data) {
  int section = CONF_SECTION_NONE;
  const char *p = data;

  while (p && *p) {
    const char *eol = strchr(p, '\n');
    int len = eol ? (int)(eol - p) : (int)strlen(p);

    int trimmed = len;
    if (trimmed > 0 && p[trimmed - 1] == '\r')
      trimmed--;

    if (trimmed == 0 || p[0] == '#' || p[0] == ';') {
      p = eol ? eol + 1 : NULL;
      continue;
    }

    if (p[0] == '[') {
      if (strncmp(p, "[colors]", 8) == 0)
        section = CONF_SECTION_COLORS;
      else if (strncmp(p, "[protocol_colors]", 17) == 0)
        section = CONF_SECTION_PROTOCOL;
      else if (strncmp(p, "[meta]", 6) == 0)
        section = CONF_SECTION_NONE;
      else
        section = CONF_SECTION_NONE;
      p = eol ? eol + 1 : NULL;
      continue;
    }

    if (section > CONF_SECTION_NONE) {
      const char *eq = memchr(p, '=', trimmed);
      if (eq) {
        int klen = (int)(eq - p);
        int vlen = trimmed - klen - 1;
        if (klen > 0 && klen < CONF_KV_BUF_SIZE && vlen > 0 && vlen < CONF_KV_BUF_SIZE) {
          char key[CONF_KV_BUF_SIZE], val[CONF_KV_BUF_SIZE];
          memcpy(key, p, klen);
          key[klen] = '\0';
          memcpy(val, eq + 1, vlen);
          val[vlen] = '\0';
          int ki = klen - 1;
          while (ki >= 0 && key[ki] == ' ')
            key[ki--] = '\0';
          char *kp = key;
          while (*kp == ' ')
            kp++;
          char *vp = val;
          while (*vp == ' ')
            vp++;
          apply_conf_color(kp, vp, section);
        }
      }
    }

    p = eol ? eol + 1 : NULL;
  }
}

static void parse_theme_json(const char *data) {
  cJSON *root = cJSON_Parse(data);
  if (root == NULL)
    return;

  cJSON *colors = cJSON_GetObjectItem(root, "colors");
  if (colors == NULL)
    colors = root;

  cJSON *v;
  v = cJSON_GetObjectItem(colors, "bg_primary");
  if (v)
    current_theme.bg_primary = lv_color_hex(hex_to_int(v->valuestring));
  v = cJSON_GetObjectItem(colors, "bg_secondary");
  if (v)
    current_theme.bg_secondary = lv_color_hex(hex_to_int(v->valuestring));
  v = cJSON_GetObjectItem(colors, "bg_item_top");
  if (v)
    current_theme.bg_item_top = lv_color_hex(hex_to_int(v->valuestring));
  v = cJSON_GetObjectItem(colors, "bg_item_bot");
  if (v)
    current_theme.bg_item_bot = lv_color_hex(hex_to_int(v->valuestring));
  v = cJSON_GetObjectItem(colors, "border_accent");
  if (v)
    current_theme.border_accent = lv_color_hex(hex_to_int(v->valuestring));
  v = cJSON_GetObjectItem(colors, "border_interface");
  if (v)
    current_theme.border_interface = lv_color_hex(hex_to_int(v->valuestring));
  v = cJSON_GetObjectItem(colors, "border_inactive");
  if (v)
    current_theme.border_inactive = lv_color_hex(hex_to_int(v->valuestring));
  v = cJSON_GetObjectItem(colors, "text_main");
  if (v)
    current_theme.text_main = lv_color_hex(hex_to_int(v->valuestring));
  v = cJSON_GetObjectItem(colors, "text_secondary");
  if (v)
    current_theme.text_secondary = lv_color_hex(hex_to_int(v->valuestring));
  v = cJSON_GetObjectItem(colors, "screen_base");
  if (v)
    current_theme.screen_base = lv_color_hex(hex_to_int(v->valuestring));

  v = cJSON_GetObjectItem(colors, "protocol_nfc");
  if (v)
    current_theme.protocol_nfc = lv_color_hex(hex_to_int(v->valuestring));
  v = cJSON_GetObjectItem(colors, "protocol_wifi");
  if (v)
    current_theme.protocol_wifi = lv_color_hex(hex_to_int(v->valuestring));
  v = cJSON_GetObjectItem(colors, "protocol_ble");
  if (v)
    current_theme.protocol_ble = lv_color_hex(hex_to_int(v->valuestring));
  v = cJSON_GetObjectItem(colors, "protocol_subghz");
  if (v)
    current_theme.protocol_subghz = lv_color_hex(hex_to_int(v->valuestring));
  v = cJSON_GetObjectItem(colors, "protocol_rfid");
  if (v)
    current_theme.protocol_rfid = lv_color_hex(hex_to_int(v->valuestring));
  v = cJSON_GetObjectItem(colors, "protocol_ir");
  if (v)
    current_theme.protocol_ir = lv_color_hex(hex_to_int(v->valuestring));
  v = cJSON_GetObjectItem(colors, "protocol_lora");
  if (v)
    current_theme.protocol_lora = lv_color_hex(hex_to_int(v->valuestring));

  cJSON_Delete(root);
}

static char *read_file_alloc(const char *path) {
  FILE *f = fopen(path, "r");
  if (f == NULL)
    return NULL;

  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);

  char *data = malloc(fsize + 1);
  if (data == NULL) {
    fclose(f);
    return NULL;
  }

  fread(data, 1, fsize, f);
  data[fsize] = 0;
  fclose(f);
  return data;
}

void ui_theme_load_from_name(const char *theme_name) {
  if (theme_name == NULL) {
    ui_theme_load_idx(0);
    return;
  }

  char path[THEME_PATH_MAX];
  char *data = NULL;
  int is_conf = 0;

  snprintf(path, sizeof(path), "/sdcard/themes/%s/theme.conf", theme_name);
  data = read_file_alloc(path);
  if (data) {
    is_conf = 1;
  } else {
    snprintf(path, sizeof(path), "/sdcard/themes/%s/theme.json", theme_name);
    data = read_file_alloc(path);
  }

  if (data == NULL) {
    ESP_LOGI(TAG, "SD theme not found (%s), using flash", theme_name);
    int idx = 0;
    for (int i = 0; i < THEME_COUNT; i++) {
      if (strcmp(theme_name, theme_names[i]) == 0) {
        idx = i;
        break;
      }
    }
    ui_theme_load_idx(idx);
    return;
  }

  ESP_LOGI(TAG, "Loading theme from SD: %s (%s)", theme_name, is_conf ? ".conf" : ".json");

  if (is_conf) {
    parse_theme_conf(data);
  } else {
    parse_theme_json(data);
  }

  free(data);

  for (int i = 0; i < THEME_COUNT; i++) {
    if (strcmp(theme_name, theme_names[i]) == 0) {
      theme_idx = i;
      break;
    }
  }

  assets_unload_sd();

  char asset_dir[ASSET_DIR_PATH_MAX];
  int total = 0;

  snprintf(asset_dir, sizeof(asset_dir), "/sdcard/themes/%s/icons", theme_name);
  total += assets_load_from_sd(asset_dir, "/assets/icons");

  snprintf(asset_dir, sizeof(asset_dir), "/sdcard/themes/%s/frames", theme_name);
  total += assets_load_from_sd(asset_dir, "/assets/frames");

  snprintf(asset_dir, sizeof(asset_dir), "/sdcard/themes/%s/fonts", theme_name);
  total += assets_load_from_sd(asset_dir, "/assets/fonts");

  snprintf(asset_dir, sizeof(asset_dir), "/sdcard/themes/%s/img", theme_name);
  total += assets_load_from_sd(asset_dir, "/assets/img");

  if (total > 0) {
    assets_manager_warm("/assets/frames");
    ESP_LOGI(TAG, "Theme '%s': loaded %d SD asset override(s)", theme_name, total);
  }
}

void ui_theme_set_protocol(protocol_id_t protocol) {
  active_protocol = protocol;
}

lv_color_t ui_theme_get_accent(void) {
  switch (active_protocol) {
    case PROTOCOL_NFC:
      return current_theme.protocol_nfc;
    case PROTOCOL_WIFI:
      return current_theme.protocol_wifi;
    case PROTOCOL_BLE:
      return current_theme.protocol_ble;
    case PROTOCOL_SUBGHZ:
      return current_theme.protocol_subghz;
    case PROTOCOL_RFID:
      return current_theme.protocol_rfid;
    case PROTOCOL_IR:
      return current_theme.protocol_ir;
    case PROTOCOL_LORA:
      return current_theme.protocol_lora;
    default:
      return current_theme.border_accent;
  }
}

void ui_theme_load_from_sd(void) {
  const char *name = g_config_screen.theme;

  if (name == NULL || name[0] == '\0') {
    ESP_LOGI(TAG, "No theme in config, using default from flash");
    ui_theme_load_idx(0);
    return;
  }

  ESP_LOGI(TAG, "Loading theme from config: %s", name);
  ui_theme_load_from_name(name);
}

void ui_theme_init(void) {
  if (g_config_screen.theme[0] != '\0') {
    ESP_LOGI(TAG, "Theme already loaded from config: %s", g_config_screen.theme);
    return;
  }

  ui_theme_load_settings();
  ui_theme_load_idx(theme_idx);
  ESP_LOGI(TAG, "Theme initialized: %s", theme_names[theme_idx]);
}
