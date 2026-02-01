#include "wifi_beacon_spam_simple_ui.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "ui_theme.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "beacon_spam.h"
#include "buzzer.h"
#include "lvgl.h"

static lv_obj_t * screen_spam = NULL;
static lv_obj_t * lbl_status = NULL;
static lv_obj_t * lbl_count = NULL;
static lv_timer_t * update_timer = NULL;
static uint32_t spam_count = 0;

extern lv_group_t * main_group;

static void update_count_cb(lv_timer_t * t) {
    (void)t;
    if (!beacon_spam_is_running()) return;
    spam_count += 10;
    if (lbl_count) {
        lv_label_set_text_fmt(lbl_count, "Created: %lu", (unsigned long)spam_count);
    }
}

static void screen_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_KEY) return;
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
        if (update_timer) {
            lv_timer_del(update_timer);
            update_timer = NULL;
        }
        beacon_spam_stop();
        buzzer_play_sound_file("buzzer_click");
        ui_switch_screen(SCREEN_WIFI_ATTACK_MENU);
    }
}

void ui_wifi_beacon_spam_simple_open(void) {
    if (screen_spam) lv_obj_del(screen_spam);

    screen_spam = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_spam, current_theme.screen_base, 0);
    lv_obj_remove_flag(screen_spam, LV_OBJ_FLAG_SCROLLABLE);

    header_ui_create(screen_spam);
    footer_ui_create(screen_spam);

    lbl_status = lv_label_create(screen_spam);
    lv_label_set_text(lbl_status, "Spamming Random SSIDs...");
    lv_obj_set_style_text_color(lbl_status, current_theme.text_main, 0);
    lv_obj_align(lbl_status, LV_ALIGN_CENTER, 0, -10);

    lbl_count = lv_label_create(screen_spam);
    lv_label_set_text(lbl_count, "Created: 0");
    lv_obj_set_style_text_color(lbl_count, current_theme.text_main, 0);
    lv_obj_align(lbl_count, LV_ALIGN_CENTER, 0, 20);

    spam_count = 0;
    beacon_spam_start_random();

    if (update_timer) lv_timer_del(update_timer);
    update_timer = lv_timer_create(update_count_cb, 1000, NULL);

    lv_obj_add_event_cb(screen_spam, screen_event_cb, LV_EVENT_KEY, NULL);

    if (main_group) {
        lv_group_add_obj(main_group, screen_spam);
        lv_group_focus_obj(screen_spam);
    }

    lv_screen_load(screen_spam);
}
