#include "ui_theme.h"
#include "assets_manager.h"
#include "cJSON.h"
#include "storage_assets.h"
#include "tos_config.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tos_flash_paths.h"
#define THEME_CONFIG_PATH FLASH_CONFIG_THEMES
#define INTERFACE_CONFIG_PATH FLASH_CONFIG_INTERFACE

static const char *TAG = "UI_THEME";

ui_theme_t current_theme;
int theme_idx = 0;
static protocol_id_t active_protocol = PROTOCOL_NONE;

const char * theme_names[] = {
    "default", "matrix", "cyber_blue", "blood", "toxic", "ghost", 
    "neon_pink", "amber", "terminal", "ice", "deep_purple", "midnight"
};

static uint32_t hex_to_int(const char * hex) {
    if (!hex) return 0;
    return (uint32_t)strtol(hex, NULL, 16);
}

void ui_theme_load_settings(void) {
    if (!storage_assets_is_mounted()) return;
    
    FILE *f = fopen(INTERFACE_CONFIG_PATH, "r");
    if (!f) return;
    
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
                for (int i = 0; i < 12; i++) {
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
    if (color_idx < 0 || color_idx > 11) color_idx = 0;

    FILE *f = fopen(THEME_CONFIG_PATH, "r");
    if (!f) {
        ESP_LOGW(TAG, "Theme file not found, using fallback");
        current_theme.screen_base     = lv_color_black();
        current_theme.text_main       = lv_color_white();
        current_theme.border_accent   = lv_color_hex(0x834EC6);
        current_theme.bg_item_top     = lv_color_black();
        current_theme.bg_item_bot     = lv_color_hex(0x2E0157);
        current_theme.border_inactive = lv_color_hex(0x404040);
        current_theme.protocol_nfc    = lv_color_hex(0x2196F3);
        current_theme.protocol_wifi   = lv_color_hex(0x834EC6);
        current_theme.protocol_ble    = lv_color_hex(0x0082FC);
        current_theme.protocol_subghz = lv_color_hex(0x4CAF50);
        current_theme.protocol_rfid   = lv_color_hex(0xFFC107);
        current_theme.protocol_ir     = lv_color_hex(0xF44336);
        current_theme.protocol_lora   = lv_color_hex(0x00BCD4);
        return;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *data = malloc(fsize + 1);
    if (!data) { fclose(f); return; }
    
    fread(data, 1, fsize, f);
    data[fsize] = 0;
    fclose(f);

    cJSON *root = cJSON_Parse(data);
    if (root) {
        cJSON *theme_node = cJSON_GetObjectItem(root, theme_names[color_idx]);
        if (theme_node) {
            current_theme.bg_primary      = lv_color_hex(hex_to_int(cJSON_GetObjectItem(theme_node, "bg_primary")->valuestring));
            current_theme.bg_secondary    = lv_color_hex(hex_to_int(cJSON_GetObjectItem(theme_node, "bg_secondary")->valuestring));
            current_theme.bg_item_top     = lv_color_hex(hex_to_int(cJSON_GetObjectItem(theme_node, "bg_item_top")->valuestring));
            current_theme.bg_item_bot     = lv_color_hex(hex_to_int(cJSON_GetObjectItem(theme_node, "bg_item_bot")->valuestring));
            current_theme.border_accent   = lv_color_hex(hex_to_int(cJSON_GetObjectItem(theme_node, "border_accent")->valuestring));
            current_theme.border_interface= lv_color_hex(hex_to_int(cJSON_GetObjectItem(theme_node, "border_interface")->valuestring));
            current_theme.border_inactive = lv_color_hex(hex_to_int(cJSON_GetObjectItem(theme_node, "border_inactive")->valuestring));
            current_theme.text_main       = lv_color_hex(hex_to_int(cJSON_GetObjectItem(theme_node, "text_main")->valuestring));
            current_theme.screen_base     = lv_color_hex(hex_to_int(cJSON_GetObjectItem(theme_node, "screen_base")->valuestring));

            cJSON *v;
            v = cJSON_GetObjectItem(theme_node, "protocol_nfc");
            current_theme.protocol_nfc    = v ? lv_color_hex(hex_to_int(v->valuestring)) : lv_color_hex(0x2196F3);
            v = cJSON_GetObjectItem(theme_node, "protocol_wifi");
            current_theme.protocol_wifi   = v ? lv_color_hex(hex_to_int(v->valuestring)) : lv_color_hex(0x834EC6);
            v = cJSON_GetObjectItem(theme_node, "protocol_ble");
            current_theme.protocol_ble    = v ? lv_color_hex(hex_to_int(v->valuestring)) : lv_color_hex(0x0082FC);
            v = cJSON_GetObjectItem(theme_node, "protocol_subghz");
            current_theme.protocol_subghz = v ? lv_color_hex(hex_to_int(v->valuestring)) : lv_color_hex(0x4CAF50);
            v = cJSON_GetObjectItem(theme_node, "protocol_rfid");
            current_theme.protocol_rfid   = v ? lv_color_hex(hex_to_int(v->valuestring)) : lv_color_hex(0xFFC107);
            v = cJSON_GetObjectItem(theme_node, "protocol_ir");
            current_theme.protocol_ir     = v ? lv_color_hex(hex_to_int(v->valuestring)) : lv_color_hex(0xF44336);
            v = cJSON_GetObjectItem(theme_node, "protocol_lora");
            current_theme.protocol_lora   = v ? lv_color_hex(hex_to_int(v->valuestring)) : lv_color_hex(0x00BCD4);
        }
        cJSON_Delete(root);
    }
    free(data);
}

static void apply_conf_color(const char *key, const char *value, int section) {
    uint32_t hex = (uint32_t)strtol(value, NULL, 16);
    lv_color_t color = lv_color_hex(hex);

    if (section == 1) { /* [colors] */
        if (strcmp(key, "bg_primary") == 0)           current_theme.bg_primary = color;
        else if (strcmp(key, "bg_secondary") == 0)    current_theme.bg_secondary = color;
        else if (strcmp(key, "bg_item_top") == 0)     current_theme.bg_item_top = color;
        else if (strcmp(key, "bg_item_bot") == 0)     current_theme.bg_item_bot = color;
        else if (strcmp(key, "border_accent") == 0)   current_theme.border_accent = color;
        else if (strcmp(key, "border_interface") == 0) current_theme.border_interface = color;
        else if (strcmp(key, "border_inactive") == 0) current_theme.border_inactive = color;
        else if (strcmp(key, "text_main") == 0)       current_theme.text_main = color;
        else if (strcmp(key, "screen_base") == 0)     current_theme.screen_base = color;
    } else if (section == 2) { /* [protocol_colors] */
        if (strcmp(key, "nfc") == 0)           current_theme.protocol_nfc = color;
        else if (strcmp(key, "wifi") == 0)     current_theme.protocol_wifi = color;
        else if (strcmp(key, "ble") == 0)      current_theme.protocol_ble = color;
        else if (strcmp(key, "subghz") == 0)   current_theme.protocol_subghz = color;
        else if (strcmp(key, "rfid") == 0)     current_theme.protocol_rfid = color;
        else if (strcmp(key, "ir") == 0)       current_theme.protocol_ir = color;
        else if (strcmp(key, "lora") == 0)     current_theme.protocol_lora = color;
    }
}

static void parse_theme_conf(const char *data) {
    /* section: 0=none/meta, 1=colors, 2=protocol_colors */
    int section = 0;
    const char *p = data;

    while (p && *p) {
        /* find end of line */
        const char *eol = strchr(p, '\n');
        int len = eol ? (int)(eol - p) : (int)strlen(p);

        /* strip trailing \r */
        int trimmed = len;
        if (trimmed > 0 && p[trimmed - 1] == '\r') trimmed--;

        /* skip empty lines and comments */
        if (trimmed == 0 || p[0] == '#' || p[0] == ';') {
            p = eol ? eol + 1 : NULL;
            continue;
        }

        /* check for section header */
        if (p[0] == '[') {
            if (strncmp(p, "[colors]", 8) == 0)               section = 1;
            else if (strncmp(p, "[protocol_colors]", 17) == 0) section = 2;
            else if (strncmp(p, "[meta]", 6) == 0)             section = 0;
            else                                                section = 0;
            p = eol ? eol + 1 : NULL;
            continue;
        }

        /* parse key=value */
        if (section > 0) {
            const char *eq = memchr(p, '=', trimmed);
            if (eq) {
                int klen = (int)(eq - p);
                int vlen = trimmed - klen - 1;
                if (klen > 0 && klen < 64 && vlen > 0 && vlen < 64) {
                    char key[64], val[64];
                    memcpy(key, p, klen);
                    key[klen] = '\0';
                    memcpy(val, eq + 1, vlen);
                    val[vlen] = '\0';
                    /* trim trailing spaces from key */
                    int ki = klen - 1;
                    while (ki >= 0 && key[ki] == ' ') key[ki--] = '\0';
                    /* trim leading spaces from key */
                    char *kp = key;
                    while (*kp == ' ') kp++;
                    /* trim leading spaces from val */
                    char *vp = val;
                    while (*vp == ' ') vp++;
                    apply_conf_color(kp, vp, section);
                }
            }
        }

        p = eol ? eol + 1 : NULL;
    }
}

static void parse_theme_json(const char *data) {
    cJSON *root = cJSON_Parse(data);
    if (!root) return;

    cJSON *colors = cJSON_GetObjectItem(root, "colors");
    if (!colors) colors = root;

    cJSON *v;
    v = cJSON_GetObjectItem(colors, "bg_primary");
    if (v) current_theme.bg_primary = lv_color_hex(hex_to_int(v->valuestring));
    v = cJSON_GetObjectItem(colors, "bg_secondary");
    if (v) current_theme.bg_secondary = lv_color_hex(hex_to_int(v->valuestring));
    v = cJSON_GetObjectItem(colors, "bg_item_top");
    if (v) current_theme.bg_item_top = lv_color_hex(hex_to_int(v->valuestring));
    v = cJSON_GetObjectItem(colors, "bg_item_bot");
    if (v) current_theme.bg_item_bot = lv_color_hex(hex_to_int(v->valuestring));
    v = cJSON_GetObjectItem(colors, "border_accent");
    if (v) current_theme.border_accent = lv_color_hex(hex_to_int(v->valuestring));
    v = cJSON_GetObjectItem(colors, "border_interface");
    if (v) current_theme.border_interface = lv_color_hex(hex_to_int(v->valuestring));
    v = cJSON_GetObjectItem(colors, "border_inactive");
    if (v) current_theme.border_inactive = lv_color_hex(hex_to_int(v->valuestring));
    v = cJSON_GetObjectItem(colors, "text_main");
    if (v) current_theme.text_main = lv_color_hex(hex_to_int(v->valuestring));
    v = cJSON_GetObjectItem(colors, "screen_base");
    if (v) current_theme.screen_base = lv_color_hex(hex_to_int(v->valuestring));

    v = cJSON_GetObjectItem(colors, "protocol_nfc");
    if (v) current_theme.protocol_nfc = lv_color_hex(hex_to_int(v->valuestring));
    v = cJSON_GetObjectItem(colors, "protocol_wifi");
    if (v) current_theme.protocol_wifi = lv_color_hex(hex_to_int(v->valuestring));
    v = cJSON_GetObjectItem(colors, "protocol_ble");
    if (v) current_theme.protocol_ble = lv_color_hex(hex_to_int(v->valuestring));
    v = cJSON_GetObjectItem(colors, "protocol_subghz");
    if (v) current_theme.protocol_subghz = lv_color_hex(hex_to_int(v->valuestring));
    v = cJSON_GetObjectItem(colors, "protocol_rfid");
    if (v) current_theme.protocol_rfid = lv_color_hex(hex_to_int(v->valuestring));
    v = cJSON_GetObjectItem(colors, "protocol_ir");
    if (v) current_theme.protocol_ir = lv_color_hex(hex_to_int(v->valuestring));
    v = cJSON_GetObjectItem(colors, "protocol_lora");
    if (v) current_theme.protocol_lora = lv_color_hex(hex_to_int(v->valuestring));

    cJSON_Delete(root);
}

static char *read_file_alloc(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *data = malloc(fsize + 1);
    if (!data) { fclose(f); return NULL; }

    fread(data, 1, fsize, f);
    data[fsize] = 0;
    fclose(f);
    return data;
}

void ui_theme_load_from_name(const char * theme_name) {
    if (!theme_name) {
        ui_theme_load_idx(0);
        return;
    }

    char path[128];
    char *data = NULL;
    int is_conf = 0;

    /* Try .conf first (new format) */
    snprintf(path, sizeof(path), "/sdcard/themes/%s/theme.conf", theme_name);
    data = read_file_alloc(path);
    if (data) {
        is_conf = 1;
    } else {
        /* Fall back to .json (legacy format) */
        snprintf(path, sizeof(path), "/sdcard/themes/%s/theme.json", theme_name);
        data = read_file_alloc(path);
    }

    if (!data) {
        ESP_LOGI(TAG, "SD theme not found (%s), using flash", theme_name);
        int idx = 0;
        for (int i = 0; i < 12; i++) {
            if (strcmp(theme_name, theme_names[i]) == 0) {
                idx = i;
                break;
            }
        }
        ui_theme_load_idx(idx);
        return;
    }

    ESP_LOGI(TAG, "Loading theme from SD: %s (%s)", theme_name,
             is_conf ? ".conf" : ".json");

    if (is_conf) {
        parse_theme_conf(data);
    } else {
        parse_theme_json(data);
    }

    free(data);

    assets_unload_sd();

    char asset_dir[160];
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
        ESP_LOGI(TAG, "Theme '%s': loaded %d SD asset override(s)", theme_name, total);
    }
}

void ui_theme_set_protocol(protocol_id_t protocol) {
    active_protocol = protocol;
}

lv_color_t ui_theme_get_accent(void) {
    switch (active_protocol) {
        case PROTOCOL_NFC:    return current_theme.protocol_nfc;
        case PROTOCOL_WIFI:   return current_theme.protocol_wifi;
        case PROTOCOL_BLE:    return current_theme.protocol_ble;
        case PROTOCOL_SUBGHZ: return current_theme.protocol_subghz;
        case PROTOCOL_RFID:   return current_theme.protocol_rfid;
        case PROTOCOL_IR:     return current_theme.protocol_ir;
        case PROTOCOL_LORA:   return current_theme.protocol_lora;
        default:              return current_theme.border_accent;
    }
}

void ui_theme_load_from_sd(void) {
    const char *name = g_config_screen.theme;

    if (!name || name[0] == '\0') {
        ESP_LOGI(TAG, "No theme in config, using default from flash");
        ui_theme_load_idx(0);
        return;
    }

    ESP_LOGI(TAG, "Loading theme from config: %s", name);
    ui_theme_load_from_name(name);
}

void ui_theme_init(void) {
    /* If tos_theme_load_from_sd() already loaded a theme from config, keep it */
    if (g_config_screen.theme[0] != '\0') {
        ESP_LOGI(TAG, "Theme already loaded from config: %s", g_config_screen.theme);
        return;
    }

    /* Fallback: load from legacy interface_config.conf */
    ui_theme_load_settings();
    ui_theme_load_idx(theme_idx);
    ESP_LOGI(TAG, "Theme initialized: %s", theme_names[theme_idx]);
}