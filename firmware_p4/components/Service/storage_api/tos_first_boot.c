// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "tos_first_boot.h"
#include "tos_config.h"
#include "tos_storage_paths.h"
#include "tos_flash_paths.h"
#include "storage_init.h"
#include "storage_impl.h"
#include "storage_mkdir.h"
#include "cJSON.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

static const char *TAG = "TOS_FIRST_BOOT";

#define THEME_CONF_BUF_SIZE 512
#define COPY_BUF_SIZE       512

static const char *const FIRST_BOOT_DIRS[] = {
    // Protocol roots
    TOS_PATH_NFC,
    TOS_PATH_RFID,
    TOS_PATH_SUBGHZ,
    TOS_PATH_IR,
    TOS_PATH_WIFI,
    TOS_PATH_BLE,
    TOS_PATH_LORA,
    TOS_PATH_BADUSB,

    // Protocol assets
    TOS_PATH_NFC_ASSETS,
    TOS_PATH_RFID_ASSETS,
    TOS_PATH_SUBGHZ_ASSETS,
    TOS_PATH_IR_ASSETS,
    TOS_PATH_WIFI_ASSETS,
    TOS_PATH_BLE_ASSETS,
    TOS_PATH_LORA_ASSETS,
    TOS_PATH_BADUSB_ASSETS,

    // WiFi loot
    TOS_PATH_WIFI_LOOT,
    TOS_PATH_WIFI_LOOT_HS,
    TOS_PATH_WIFI_LOOT_PCAPS,
    TOS_PATH_WIFI_LOOT_DEAUTH,

    // BLE & LoRa loot
    TOS_PATH_BLE_LOOT,
    TOS_PATH_LORA_LOOT,

    // LoRa messages
    TOS_PATH_LORA_MSGS,

    // Captive portal
    TOS_PATH_CAPTIVE,
    TOS_PATH_CAPTIVE_TMPL,

    // Config
    TOS_PATH_CONFIG_DIR,

    // System
    TOS_PATH_THEMES,
    TOS_PATH_RINGTONES,
    TOS_PATH_APPS,
    TOS_PATH_APPS_DATA,
    TOS_PATH_SCRIPTS,
    TOS_PATH_LOGS,
    TOS_PATH_BACKUP,
    TOS_PATH_CACHE,
    TOS_PATH_UPDATE,

    NULL};

static bool setup_already_done(void) {
  struct stat st;
  return (stat(TOS_PATH_SETUP_MARKER, &st) == 0);
}

static void mark_setup_done(void) {
  FILE *f = fopen(TOS_PATH_SETUP_MARKER, "w");
  if (f) {
    fclose(f);
    ESP_LOGI(TAG, "Marker created: %s", TOS_PATH_SETUP_MARKER);
  } else {
    ESP_LOGW(TAG, "Failed to create marker: %s", TOS_PATH_SETUP_MARKER);
  }
}

// Default protocol colors applied to every migrated theme
static const char *DEFAULT_PROTOCOL_COLORS = "[protocol_colors]\n"
                                             "nfc    = 0x3498DB\n"
                                             "wifi   = 0x9B59B6\n"
                                             "ble    = 0x1ABC9C\n"
                                             "subghz = 0x2ECC71\n"
                                             "rfid   = 0xE67E22\n"
                                             "ir     = 0xE74C3C\n"
                                             "lora   = 0xF1C40F\n";

static const char *COLOR_KEYS[] = {"bg_primary",
                                   "bg_secondary",
                                   "bg_item_top",
                                   "bg_item_bot",
                                   "border_accent",
                                   "border_interface",
                                   "border_inactive",
                                   "text_main",
                                   "screen_base",
                                   NULL};

static char *read_file_to_str(const char *path) {
  FILE *f = fopen(path, "r");
  if (f == NULL)
    return NULL;

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (size <= 0) {
    fclose(f);
    return NULL;
  }

  char *buf = malloc(size + 1);
  if (buf == NULL) {
    fclose(f);
    return NULL;
  }

  fread(buf, 1, size, f);
  buf[size] = '\0';
  fclose(f);
  return buf;
}

static void migrate_themes_to_sd(void) {
  // Only migrate if default theme doesn't exist yet
  struct stat st;
  if (stat(TOS_PATH_THEMES "/default/theme.conf", &st) == 0) {
    return;
  }

  char *json_str = read_file_to_str(FLASH_CONFIG_THEMES);
  if (json_str == NULL) {
    ESP_LOGW(TAG, "Cannot read %s for theme migration", FLASH_CONFIG_THEMES);
    return;
  }

  cJSON *root = cJSON_Parse(json_str);
  free(json_str);
  if (root == NULL) {
    ESP_LOGE(TAG, "Failed to parse themes JSON");
    return;
  }

  int count = 0;
  cJSON *theme = NULL;
  cJSON_ArrayForEach(theme, root) {
    const char *name = theme->string;
    if (name == NULL)
      continue;

    // Create theme dir
    char dir[96];
    snprintf(dir, sizeof(dir), TOS_PATH_THEMES "/%s", name);
    storage_mkdir_recursive(dir);

    // Build theme.conf content
    char conf[THEME_CONF_BUF_SIZE];
    int off = 0;

    // [meta]
    off += snprintf(conf + off,
                    sizeof(conf) - off,
                    "[meta]\nname    = %s\nauthor  = High Code\nversion = 1.0.0\n\n",
                    name);

    // [colors]
    off += snprintf(conf + off, sizeof(conf) - off, "[colors]\n");
    for (int i = 0; COLOR_KEYS[i]; i++) {
      cJSON *v = cJSON_GetObjectItem(theme, COLOR_KEYS[i]);
      if (cJSON_IsString(v) && v->valuestring) {
        off +=
            snprintf(conf + off, sizeof(conf) - off, "%-16s = %s\n", COLOR_KEYS[i], v->valuestring);
      }
    }

    // [protocol_colors] — defaults
    off += snprintf(conf + off, sizeof(conf) - off, "\n%s", DEFAULT_PROTOCOL_COLORS);

    // Write file
    char filepath[128];
    snprintf(filepath, sizeof(filepath), "%s/theme.conf", dir);
    FILE *f = fopen(filepath, "w");
    if (f) {
      fputs(conf, f);
      fclose(f);
      count++;
    }
  }

  cJSON_Delete(root);
  ESP_LOGI(TAG, "Migrated %d themes to SD", count);
}

// Assets to copy from flash → SD on first boot (never overwrite)
typedef struct {
  const char *src; // flash path (/assets/...)
  const char *dst; // SD path (/sdcard/...)
} asset_copy_t;

static const asset_copy_t FIRST_BOOT_ASSETS[] = {
    // Captive portal HTML templates
    {FLASH_CAPTIVE_TEMPLATES "/evil_twin_index.html",
     TOS_PATH_CAPTIVE_TMPL "/evil_twin_index.html"},
    {FLASH_CAPTIVE_TEMPLATES "/evil_twin_thank_you.html",
     TOS_PATH_CAPTIVE_TMPL "/evil_twin_thank_you.html"},

    // Captive portal passwords
    {FLASH_MOUNT "/storage/captive_portal/passwords.json", TOS_PATH_CAPTIVE "/passwords.json"},

    // BadUSB example scripts
    {FLASH_STORAGE_BADUSB "/rickroll.txt", TOS_PATH_BADUSB_ASSETS "/rickroll.txt"},

    // TODO: add protocol database entries here when files are added to flash
    // e.g. mf_classic_dict.nfc, oui_db.csv, frequency_list.sub, etc.

    {NULL, NULL}};

static esp_err_t copy_file_cross_fs(const char *src, const char *dst) {
  FILE *fin = fopen(src, "r");
  if (fin == NULL)
    return ESP_ERR_NOT_FOUND;

  FILE *fout = fopen(dst, "w");
  if (fout == NULL) {
    fclose(fin);
    return ESP_FAIL;
  }

  char buf[COPY_BUF_SIZE];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), fin)) > 0) {
    if (fwrite(buf, 1, n, fout) != n) {
      fclose(fin);
      fclose(fout);
      return ESP_FAIL;
    }
  }

  fclose(fin);
  fclose(fout);
  return ESP_OK;
}

static void copy_flash_assets_to_sd(void) {
  int copied = 0, skipped = 0;

  for (int i = 0; FIRST_BOOT_ASSETS[i].src != NULL; i++) {
    // Never overwrite existing user files
    struct stat st;
    if (stat(FIRST_BOOT_ASSETS[i].dst, &st) == 0) {
      continue;
    }

    esp_err_t ret = copy_file_cross_fs(FIRST_BOOT_ASSETS[i].src, FIRST_BOOT_ASSETS[i].dst);
    if (ret == ESP_OK) {
      copied++;
    } else if (ret == ESP_ERR_NOT_FOUND) {
      skipped++; // source doesn't exist in flash yet
    } else {
      ESP_LOGW(
          TAG, "Asset copy failed: %s -> %s", FIRST_BOOT_ASSETS[i].src, FIRST_BOOT_ASSETS[i].dst);
    }
  }

  if (copied > 0) {
    ESP_LOGI(TAG, "Copied %d flash assets to SD (%d sources not found)", copied, skipped);
  }
}

esp_err_t tos_first_boot_setup(void) {
  if (!storage_is_mounted()) {
    ESP_LOGW(TAG, "Storage not mounted, skipping first boot setup");
    return ESP_ERR_INVALID_STATE;
  }

  if (setup_already_done()) {
    ESP_LOGI(TAG, "Setup already done, skipping");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "First boot detected — creating folder structure");

  int created = 0;
  int failed = 0;

  for (int i = 0; FIRST_BOOT_DIRS[i] != NULL; i++) {
    esp_err_t ret = storage_mkdir_recursive(FIRST_BOOT_DIRS[i]);
    if (ret == ESP_OK) {
      created++;
    } else {
      ESP_LOGW(TAG, "Failed to create: %s (%s)", FIRST_BOOT_DIRS[i], esp_err_to_name(ret));
      failed++;
    }
  }

  ESP_LOGI(TAG, "Folders: %d created, %d failed", created, failed);

  // Create default .conf files on SD if they don't exist
  struct stat st;
  const char *confs[] = {"screen", "wifi", "ble", "lora", "system"};
  const char *paths[] = {TOS_PATH_CONFIG_SCREEN,
                         TOS_PATH_CONFIG_WIFI,
                         TOS_PATH_CONFIG_BLE,
                         TOS_PATH_CONFIG_LORA,
                         TOS_PATH_CONFIG_SYSTEM};

  for (size_t i = 0; i < sizeof(confs) / sizeof(confs[0]); i++) {
    if (stat(paths[i], &st) != 0) {
      tos_config_save(paths[i], confs[i]);
      ESP_LOGI(TAG, "Created default: %s", paths[i]);
    }
  }

  // Migrate 12 built-in themes to SD as individual theme.conf files
  migrate_themes_to_sd();

  // Copy flash assets to SD (templates, example scripts, etc.)
  copy_flash_assets_to_sd();

  if (failed == 0) {
    mark_setup_done();
  }

  return (failed == 0) ? ESP_OK : ESP_FAIL;
}
