#include "home_ui.h"
#include "core/lv_group.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "header_ui.h"
#include "footer_ui.h"

static lv_obj_t * menu_screen = NULL;
static lv_obj_t * menu_indicator_label = NULL;

static const char * menu_items[] = {
    "BLE",
    "WIFI",
    "IR",
    "CONFIGS"
};

#define MENU_ITEM_COUNT (sizeof(menu_items) / sizeof(menu_items[0]))

static uint8_t menu_index = 1;
static lv_obj_t * menu_dots[MENU_ITEM_COUNT];

static void menu_update_ui(void)
{
    lv_label_set_text_fmt(
        menu_indicator_label,
        LV_SYMBOL_LEFT "   %s   " LV_SYMBOL_RIGHT,
        menu_items[menu_index]
    );

    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        lv_obj_set_size(
            menu_dots[i],
            (i == menu_index) ? 10 : 6,
            (i == menu_index) ? 10 : 6
        );

        lv_obj_set_style_bg_opa(
            menu_dots[i],
            (i == menu_index) ? LV_OPA_COVER : LV_OPA_50,
            0
        );
    }
}

static void menu_event_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_KEY)
        return;

    uint32_t key = lv_event_get_key(e);

    if (key == LV_KEY_RIGHT) {
        menu_index = (menu_index + 1) % MENU_ITEM_COUNT;
        menu_update_ui();
    }
    else if (key == LV_KEY_LEFT) {
        menu_index = (menu_index == 0)
                     ? MENU_ITEM_COUNT - 1
                     : menu_index - 1;
        menu_update_ui();
    }
}

void ui_menu_open(void)
{
    if (menu_screen) {
        lv_obj_del(menu_screen);
        menu_screen = NULL;
    }

    menu_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(menu_screen, lv_color_black(), 0);
    lv_obj_remove_flag(menu_screen, LV_OBJ_FLAG_SCROLLABLE);

    header_ui_create(menu_screen);

    lv_obj_t * menu_dots_container = lv_obj_create(menu_screen);
    lv_obj_set_size(menu_dots_container, LV_SIZE_CONTENT, 8);
    lv_obj_align(menu_dots_container, LV_ALIGN_TOP_MID, 0, 42);
    lv_obj_set_flex_flow(menu_dots_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        menu_dots_container,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );
    lv_obj_set_style_bg_opa(menu_dots_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(menu_dots_container, 0, 0);
    lv_obj_set_style_pad_gap(menu_dots_container, 6, 0);
    lv_obj_remove_flag(menu_dots_container, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        menu_dots[i] = lv_obj_create(menu_dots_container);
        lv_obj_set_size(menu_dots[i], 6, 6);
        lv_obj_set_style_radius(menu_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(menu_dots[i], lv_color_white(), 0);
        lv_obj_set_style_border_width(menu_dots[i], 0, 0);
    }

    lv_obj_t * img_left4 = lv_img_create(menu_screen);
    lv_img_set_src(img_left4, "A:/frames/nfc_frame_1.png");
    lv_img_set_zoom(img_left4, 460);
    lv_obj_align(img_left4, LV_ALIGN_CENTER, -140, 0);
    lv_obj_set_style_transform_width(img_left4, lv_obj_get_width(img_left4) / 4, 0);
    lv_obj_set_style_opa(img_left4, LV_OPA_30, 0);

    lv_obj_t * img_left3 = lv_img_create(menu_screen);
    lv_img_set_src(img_left3, "A:/frames/nfc_frame_1.png");
    lv_img_set_zoom(img_left3, 480);
    lv_obj_align(img_left3, LV_ALIGN_CENTER, -105, 0);
    lv_obj_set_style_transform_width(img_left3, lv_obj_get_width(img_left3) / 3, 0);
    lv_obj_set_style_opa(img_left3, LV_OPA_40, 0);

    lv_obj_t * img_left2 = lv_img_create(menu_screen);
    lv_img_set_src(img_left2, "A:/frames/nfc_frame_1.png");
    lv_img_set_zoom(img_left2, 500);
    lv_obj_align(img_left2, LV_ALIGN_CENTER, -70, 0);
    lv_obj_set_style_transform_width(img_left2, lv_obj_get_width(img_left2) / 2, 0);
    lv_obj_set_style_opa(img_left2, LV_OPA_60, 0);

    lv_obj_t * img_left1 = lv_img_create(menu_screen);
    lv_img_set_src(img_left1, "A:/frames/nfc_frame_1.png");
    lv_img_set_zoom(img_left1, 520);
    lv_obj_align(img_left1, LV_ALIGN_CENTER, -35, 0);
    lv_obj_set_style_transform_width(img_left1, lv_obj_get_width(img_left1) * 3 / 4, 0);
    lv_obj_set_style_opa(img_left1, LV_OPA_80, 0);

    lv_obj_t * img_right4 = lv_img_create(menu_screen);
    lv_img_set_src(img_right4, "A:/frames/nfc_frame_2.png");
    lv_img_set_antialias(img_right4, true);
    lv_img_set_zoom(img_right4, 460);
    lv_obj_align(img_right4, LV_ALIGN_CENTER, 140, 0);
    lv_obj_set_style_transform_pivot_x(img_right4, lv_obj_get_width(img_right4) / 2, 0);
    lv_obj_set_style_transform_width(img_right4, -lv_obj_get_width(img_right4) / 4, 0);
    lv_obj_set_style_opa(img_right4, LV_OPA_30, 0);

    lv_obj_t * img_right3 = lv_img_create(menu_screen);
    lv_img_set_src(img_right3, "A:/frames/nfc_frame_2.png");
    lv_img_set_antialias(img_right3, true);
    lv_img_set_zoom(img_right3, 480);
    lv_obj_align(img_right3, LV_ALIGN_CENTER, 105, 0);
    lv_obj_set_style_transform_pivot_x(img_right3, lv_obj_get_width(img_right3) / 2, 0);
    lv_obj_set_style_transform_width(img_right3, -lv_obj_get_width(img_right3) / 3, 0);
    lv_obj_set_style_opa(img_right3, LV_OPA_40, 0);

    lv_obj_t * img_right2 = lv_img_create(menu_screen);
    lv_img_set_src(img_right2, "A:/frames/nfc_frame_2.png");
    lv_img_set_antialias(img_right2, true);
    lv_img_set_zoom(img_right2, 500);
    lv_obj_align(img_right2, LV_ALIGN_CENTER, 70, 0);
    lv_obj_set_style_transform_pivot_x(img_right2, lv_obj_get_width(img_right2) / 2, 0);
    lv_obj_set_style_transform_width(img_right2, -lv_obj_get_width(img_right2) / 2, 0);
    lv_obj_set_style_opa(img_right2, LV_OPA_60, 0);

    lv_obj_t * img_right1 = lv_img_create(menu_screen);
    lv_img_set_src(img_right1, "A:/frames/nfc_frame_2.png");
    lv_img_set_antialias(img_right1, true);
    lv_img_set_zoom(img_right1, 520);
    lv_obj_align(img_right1, LV_ALIGN_CENTER, 35, 0);
    lv_obj_set_style_transform_pivot_x(img_right1, lv_obj_get_width(img_right1) / 2, 0);
    lv_obj_set_style_transform_width(img_right1, -lv_obj_get_width(img_right1) * 3 / 4, 0);
    lv_obj_set_style_opa(img_right1, LV_OPA_80, 0);

    lv_obj_t * img_center = lv_img_create(menu_screen);
    lv_img_set_src(img_center, "A:/frames/nfc_frame_0.png");
    lv_img_set_zoom(img_center, 750);
    lv_obj_align(img_center, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_opa(img_center, LV_OPA_COVER, 0);

    menu_indicator_label = lv_label_create(menu_screen);
    lv_obj_align(menu_indicator_label, LV_ALIGN_BOTTOM_MID, 0, -42);
    lv_obj_set_style_text_color(menu_indicator_label, lv_color_white(), 0);
    lv_obj_set_style_text_opa(menu_indicator_label, LV_OPA_80, 0);

    menu_update_ui();

    footer_ui_create(menu_screen);

    lv_obj_add_event_cb(menu_screen, menu_event_cb, LV_EVENT_KEY, NULL);

    if (main_group) {
        lv_group_add_obj(main_group, menu_screen);
        lv_group_focus_obj(menu_screen);
    }

    lv_screen_load(menu_screen);
}
