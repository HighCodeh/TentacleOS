#include "ui_theme.h"
#include "cJSON.h"
#include "storage_assets.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define THEME_CONFIG_PATH "/assets/config/screen/screen_themes.conf"
#define INTERFACE_CONFIG_PATH "/assets/config/screen/interface_config.conf"

static const char *TAG = "UI_THEME";

ui_theme_t current_theme;
int theme_idx = 0;

const char * theme_names[] = {
    "default", "matrix", "cyber_blue", "blood", "toxic", "ghost", 
    "neon_pink", "amber", "terminal", "ice", "deep_purple", "midnight"
};

static uint32_t hex_to_int(const char * hex) {
    if (!hex) return 0;
    return (uint32_t)strtol(hex, NULL, 16);
}

// NOVO: Carrega especificamente o índice do tema de forma independente
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
        ESP_LOGW(TAG, "Arquivo de temas nao encontrado. Usando fallback.");
        current_theme.screen_base     = lv_color_black();
        current_theme.text_main       = lv_color_white();
        current_theme.border_accent   = lv_color_hex(0x834EC6);
        current_theme.bg_item_top     = lv_color_black();
        current_theme.bg_item_bot     = lv_color_hex(0x2E0157);
        current_theme.border_inactive = lv_color_hex(0x404040);
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
        }
        cJSON_Delete(root);
    }
    free(data);
}

void ui_theme_init(void) {
    ui_theme_load_settings(); // Carrega o índice salvo
    ui_theme_load_idx(theme_idx); // Aplica as cores
    ESP_LOGI(TAG, "Tema inicializado: %s", theme_names[theme_idx]);
}