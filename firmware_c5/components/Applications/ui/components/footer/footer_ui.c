#include "footer_ui.h"
#include "ui_theme.h"
#include "cJSON.h"
#include "storage_assets.h"
#include "esp_log.h"

#define FOOTER_HEIGHT 20
#define INTERFACE_CONFIG_PATH "/assets/config/screen/interface_config.conf"

static bool footer_is_hidden(void) {
    bool hidden = false;
    if (!storage_assets_is_mounted()) return hidden;
    FILE *f = fopen(INTERFACE_CONFIG_PATH, "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *data = malloc(fsize + 1);
        if (data) {
            fread(data, 1, fsize, f);
            data[fsize] = 0;
            cJSON *root = cJSON_Parse(data);
            if (root) {
                cJSON *f_sw = cJSON_GetObjectItem(root, "hide_footer");
                if (cJSON_IsBool(f_sw)) hidden = cJSON_IsTrue(f_sw);
                cJSON_Delete(root);
            }
            free(data);
        }
        fclose(f);
    }
    return hidden;
}

void footer_ui_create(lv_obj_t * parent) {
    if (footer_is_hidden()) return;
    
    lv_obj_t * footer = lv_obj_create(parent);
    lv_obj_set_size(footer, lv_pct(100), FOOTER_HEIGHT);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(footer, current_theme.bg_primary, 0);
    lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(footer, 2, 0);
    lv_obj_set_style_border_color(footer, current_theme.border_interface, 0);
    lv_obj_set_style_radius(footer, 0, 0);
    lv_obj_set_style_pad_all(footer, 0, 0);
}