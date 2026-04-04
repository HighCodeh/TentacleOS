#include "header_ui.h"
#include "ui_theme.h"
#include "wifi_service.h"
#include "assets_manager.h"
#include "lvgl.h"
#include "st7789.h"

#define HEADER_HEIGHT ((LCD_V_RES * 9) / 100)

static lv_font_t * inter_font = NULL;

static bool header_wifi_connected = false;
static bool header_wifi_enabled = true;
static lv_timer_t * wifi_status_timer = NULL;

static lv_obj_t * wifi_img = NULL;
static lv_image_dsc_t * wifi_dscs[4] = {NULL};
static int wifi_frame = 0;
static int wifi_dir = 1;
static lv_timer_t * wifi_anim_timer = NULL;

static const char * wifi_paths[4] = {
    "/assets/icons/wifi_icon_0.bin",
    "/assets/icons/wifi_icon_1.bin",
    "/assets/icons/wifi_icon_2.bin",
    "/assets/icons/wifi_icon_3.bin",
};

static lv_obj_t * battery_img = NULL;
static lv_obj_t * power_img = NULL;
static lv_image_dsc_t * battery_dscs[4] = {NULL};
static lv_image_dsc_t * power_icon_dsc = NULL;
static int battery_frame = 0;
static int battery_dir = 1;
static lv_timer_t * battery_anim_timer = NULL;

static const char * battery_paths[4] = {
    "/assets/icons/battery_1.bin",
    "/assets/icons/battery_2.bin",
    "/assets/icons/battery_3.bin",
    "/assets/icons/battery_4.bin",
};

static void header_wifi_status_timer_cb(lv_timer_t * timer)
{
    if (wifi_status_timer && wifi_img && !lv_obj_is_valid(wifi_img)) {
        lv_timer_delete(timer);
        wifi_status_timer = NULL;
        return;
    }
    bool current_active = wifi_service_is_active();
    bool current_connected = wifi_service_is_connected();

    if (current_active != header_wifi_enabled || current_connected != header_wifi_connected) {
        header_wifi_enabled = current_active;
        header_wifi_connected = current_connected;
    }
}

static void wifi_anim_timer_cb(lv_timer_t * timer)
{
    if (!wifi_img || !lv_obj_is_valid(wifi_img)) {
        lv_timer_delete(timer);
        wifi_anim_timer = NULL;
        wifi_img = NULL;
        return;
    }

    wifi_frame += wifi_dir;
    if (wifi_frame >= 3) { wifi_frame = 3; wifi_dir = -1; }
    if (wifi_frame <= 0) { wifi_frame = 0; wifi_dir = 1; }

    if (wifi_dscs[wifi_frame]) {
        lv_image_set_src(wifi_img, wifi_dscs[wifi_frame]);
    }
}

static void battery_anim_timer_cb(lv_timer_t * timer)
{
    if (!battery_img || !lv_obj_is_valid(battery_img)) {
        lv_timer_delete(timer);
        battery_anim_timer = NULL;
        battery_img = NULL;
        power_img = NULL;
        return;
    }

    battery_frame += battery_dir;
    if (battery_frame >= 3) { battery_frame = 3; battery_dir = -1; }
    if (battery_frame <= 0) { battery_frame = 0; battery_dir = 1; }

    if (battery_dscs[battery_frame]) {
        lv_image_set_src(battery_img, battery_dscs[battery_frame]);
    }

    if (power_img) {
        if (battery_dir == 1) {
            lv_obj_remove_flag(power_img, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(power_img, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void header_ui_create(lv_obj_t * parent)
{
    lv_obj_t * header = lv_obj_create(parent);
    lv_obj_set_size(header, lv_pct(100), HEADER_HEIGHT + 12);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, -12);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_radius(header, 12, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);

    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(header, current_theme.bg_primary, 0);
    lv_obj_set_style_bg_grad_color(header, current_theme.bg_secondary, 0);
    lv_obj_set_style_bg_grad_dir(header, LV_GRAD_DIR_HOR, 0);

    if (!inter_font) {
        inter_font = lv_binfont_create("A:assets/fonts/Inter.bin");
    }

    lv_obj_t * lbl_time = lv_label_create(header);
    lv_label_set_text(lbl_time, "12:00");
    lv_obj_set_style_text_color(lbl_time, current_theme.text_main, 0);
    lv_obj_set_style_text_font(lbl_time, inter_font ? inter_font : &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_time, LV_ALIGN_LEFT_MID, 6, 6);

    lv_obj_t * icon_cont = lv_obj_create(header);
    lv_obj_set_size(icon_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(icon_cont, LV_ALIGN_RIGHT_MID, -6, 6);
    lv_obj_set_flex_flow(icon_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(icon_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(icon_cont, 10, 0);
    lv_obj_set_style_pad_all(icon_cont, 0, 0);
    lv_obj_set_style_bg_opa(icon_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(icon_cont, 0, 0);

    static lv_image_dsc_t * bt_icon_dsc = NULL;
    static lv_image_dsc_t * card_icon_dsc = NULL;

    for (int i = 0; i < 4; i++) {
        if (!wifi_dscs[i]) wifi_dscs[i] = assets_get(wifi_paths[i]);
    }
    if (!bt_icon_dsc) bt_icon_dsc = assets_get("/assets/icons/bluetooth_icon.bin");
    if (!card_icon_dsc) card_icon_dsc = assets_get("/assets/icons/card_icon.bin");

    wifi_img = lv_image_create(icon_cont);
    if (wifi_dscs[0]) lv_image_set_src(wifi_img, wifi_dscs[0]);

    lv_obj_t * bt_img = lv_image_create(icon_cont);
    if (bt_icon_dsc) lv_image_set_src(bt_img, bt_icon_dsc);

    lv_obj_t * card_img = lv_image_create(icon_cont);
    if (card_icon_dsc) lv_image_set_src(card_img, card_icon_dsc);

    for (int i = 0; i < 4; i++) {
        if (!battery_dscs[i]) battery_dscs[i] = assets_get(battery_paths[i]);
    }
    if (!power_icon_dsc) power_icon_dsc = assets_get("/assets/icons/power_icon.bin");

    lv_obj_t * bat_cont = lv_obj_create(icon_cont);
    lv_obj_set_size(bat_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(bat_cont, 0, 0);
    lv_obj_set_style_bg_opa(bat_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bat_cont, 0, 0);

    battery_img = lv_image_create(bat_cont);
    if (battery_dscs[0]) lv_image_set_src(battery_img, battery_dscs[0]);
    lv_obj_center(battery_img);

    power_img = lv_image_create(bat_cont);
    if (power_icon_dsc) lv_image_set_src(power_img, power_icon_dsc);
    lv_obj_center(power_img);

    if (wifi_anim_timer == NULL) {
        wifi_anim_timer = lv_timer_create(wifi_anim_timer_cb, 800, NULL);
    }

    if (battery_anim_timer == NULL) {
        battery_anim_timer = lv_timer_create(battery_anim_timer_cb, 800, NULL);
    }

    header_wifi_enabled = wifi_service_is_active();
    header_wifi_connected = wifi_service_is_connected();

    if (wifi_status_timer == NULL) {
        wifi_status_timer = lv_timer_create(header_wifi_status_timer_cb, 500, NULL);
    }
}
