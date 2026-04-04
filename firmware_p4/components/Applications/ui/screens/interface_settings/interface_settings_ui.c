#include "ui_theme.h"
#include "interface_settings_ui.h"
#include "menu_component_ui.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "buttons_gpio.h"
#include "esp_log.h"
#include <stdio.h>
#include "cJSON.h"
#include "storage_assets.h"
#include <sys/stat.h>
#include <string.h>

#include "tos_flash_paths.h"
#define INTERFACE_CONFIG_PATH FLASH_CONFIG_INTERFACE

static lv_obj_t * screen_interface = NULL;
static menu_component_t menu;
static lv_timer_t * nav_timer = NULL;

static bool btn_up_last = false;
static bool btn_down_last = false;
static bool btn_left_last = false;
static bool btn_right_last = false;
static bool btn_ok_last = false;
static bool btn_back_last = false;

int header_idx = 0;
const char * header_options[] = {"DEFAULT", "GD TOP", "GD BOT", "MINIMAL"};
#define HEADER_COUNT 4

bool hide_footer = false;

static int lang_idx = 0;
const char * lang_options[] = {"EN", "PT", "ES", "FR"};

void interface_save_config(void) {
    if (!storage_assets_is_mounted()) return;
    mkdir("/assets/config", 0777);
    mkdir(FLASH_MOUNT "/config/screen", 0777);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "header_idx", header_idx);
    cJSON_AddBoolToObject(root, "hide_footer", hide_footer);
    cJSON_AddNumberToObject(root, "lang_idx", lang_idx);

    char *out = cJSON_PrintUnformatted(root);
    if (out) {
        FILE *f = fopen(INTERFACE_CONFIG_PATH, "w");
        if (f) { fputs(out, f); fclose(f); }
        cJSON_free(out);
    }
    cJSON_Delete(root);
}

void interface_load_config(void) {
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
            cJSON *h = cJSON_GetObjectItem(root, "header_idx");
            cJSON *f_sw = cJSON_GetObjectItem(root, "hide_footer");
            cJSON *l = cJSON_GetObjectItem(root, "lang_idx");
            if (cJSON_IsNumber(h)) header_idx = h->valueint;
            if (cJSON_IsBool(f_sw)) hide_footer = cJSON_IsTrue(f_sw);
            if (cJSON_IsNumber(l)) lang_idx = l->valueint;
            cJSON_Delete(root);
        }
        free(data);
    }
    fclose(f);
}

enum {
    ITEM_THEME  = 0,
    ITEM_HEADER = 1,
    ITEM_FOOTER = 2,
    ITEM_COUNT  = 3,
};

static void nav_timer_cb(lv_timer_t * t) {
    if (lv_screen_active() != screen_interface) {
        lv_timer_delete(t);
        nav_timer = NULL;
        return;
    }
    if (ui_input_is_locked()) return;
    bool up    = up_button_is_down();
    bool down  = down_button_is_down();
    bool left  = left_button_is_down();
    bool right = right_button_is_down();
    bool ok    = ok_button_is_down();
    bool back  = back_button_is_down();

    /* DOWN / UP — navigate between items */
    if (down && !btn_down_last) {
        menu_component_next(&menu);
    }
    if (up && !btn_up_last) {
        menu_component_prev(&menu);
    }

    /* BACK — return to settings */
    if (back && !btn_back_last) {
        btn_back_last = back;
        ui_switch_screen(SCREEN_SETTINGS);
        return;
    }

    /* OK — open theme selector screen */
    if (ok && !btn_ok_last) {
        int sel = menu_component_get_selected(&menu);
        if (sel == ITEM_THEME) {
            btn_ok_last = ok;
            ui_switch_screen(SCREEN_THEME_SELECTOR);
            return;
        }
    }

    /* LEFT / RIGHT — change selector values */
    if ((left && !btn_left_last) || (right && !btn_right_last)) {
        int sel = menu_component_get_selected(&menu);
        int dir = (right && !btn_right_last) ? 1 : -1;

        switch (sel) {
        case ITEM_THEME:
            /* Theme is now selected via dedicated screen (OK button) */
            break;

        case ITEM_HEADER:
            header_idx = (header_idx + dir + HEADER_COUNT) % HEADER_COUNT;
            menu_component_set_selector_value(&menu, ITEM_HEADER, header_options[header_idx]);
            interface_save_config();
            break;

        case ITEM_FOOTER:
            menu_component_toggle_item(&menu, ITEM_FOOTER);
            hide_footer = !menu_component_get_toggle(&menu, ITEM_FOOTER);
            interface_save_config();
            break;

        default:
            break;
        }
    }

    btn_up_last    = up;
    btn_down_last  = down;
    btn_left_last  = left;
    btn_right_last = right;
    btn_ok_last    = ok;
    btn_back_last  = back;
}

void ui_interface_settings_open(void) {
    interface_load_config();

    if (screen_interface) { lv_obj_del(screen_interface); screen_interface = NULL; }

    screen_interface = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_interface, current_theme.screen_base, 0);
    lv_obj_set_style_bg_opa(screen_interface, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen_interface, LV_OBJ_FLAG_SCROLLABLE);

    menu = menu_component_create(screen_interface, "INTERFACE", NULL);

    menu_component_add_item(&menu, "/assets/icons/theme_menu_icon.bin", "THEME");
    menu_component_add_selector(&menu, "/assets/icons/header_menu_icon.bin", "HEADER", header_options[header_idx]);
    menu_component_add_toggle(&menu, NULL, "FOOTER", !hide_footer);

    if (nav_timer == NULL) {
        nav_timer = lv_timer_create(nav_timer_cb, 50, NULL);
    }

    lv_screen_load(screen_interface);
}
