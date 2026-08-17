extern "C" {
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
}

#include <lvgl.h>

extern "C" {
bool up_button_is_down(void);
bool down_button_is_down(void);
bool left_button_is_down(void);
bool right_button_is_down(void);
bool ok_button_is_down(void);
bool back_button_is_down(void);
}

static const char *TAG = "LV_PORT_INDEV";

static bool s_is_keyboard_mode = false;

static void keypad_read(lv_indev_t *indev, lv_indev_data_t *data);
static uint32_t keypad_get_key(void);

extern "C" {

lv_indev_t *indev_keypad = nullptr;
lv_group_t *main_group = nullptr;

void lv_port_indev_init(void) {
    indev_keypad = lv_indev_create();
    lv_indev_set_type(indev_keypad, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev_keypad, keypad_read);

    main_group = lv_group_create();
    lv_group_set_default(main_group);
    lv_indev_set_group(indev_keypad, main_group);

    ESP_LOGI(TAG, "HLE input device initialized");
}

void lv_port_indev_set_keyboard_mode(bool is_enabled) {
    s_is_keyboard_mode = is_enabled;
}

void hle_lv_port_indev_init(void) { lv_port_indev_init(); }

} // extern "C"

static void keypad_read(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    static uint32_t s_last_key = 0;

    const uint32_t key = keypad_get_key();

    if (key != 0) {
        data->state = LV_INDEV_STATE_PRESSED;
        s_last_key = key;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }

    data->key = s_last_key;
}

static uint32_t keypad_get_key(void) {
    if (up_button_is_down()) {
        return s_is_keyboard_mode ? LV_KEY_UP : LV_KEY_PREV;
    }
    if (down_button_is_down()) {
        return s_is_keyboard_mode ? LV_KEY_DOWN : LV_KEY_NEXT;
    }
    if (ok_button_is_down()) {
        return LV_KEY_ENTER;
    }
    if (back_button_is_down()) {
        return LV_KEY_ESC;
    }
    if (left_button_is_down()) {
        return LV_KEY_LEFT;
    }
    if (right_button_is_down()) {
        return LV_KEY_RIGHT;
    }

    return 0;
}
