#include "ui_badusb_running.h"
#include "ui_manager.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "lv_port_indev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ducky_parser.h"
#include "bad_usb.h"

static const char *TAG = "UI_BADUSB_RUNNING";
static lv_obj_t * screen_running = NULL;
static lv_obj_t * progress_bar = NULL;
static TaskHandle_t script_task_handle = NULL;
static char script_name[64] = "rickroll.txt"; // Placeholder

void ui_badusb_running_set_script(const char *name) {
    if (name) {
        strncpy(script_name, name, sizeof(script_name) - 1);
        script_name[sizeof(script_name) - 1] = '\0';
    }
}

static void ducky_progress_callback(int current_line, int total_lines) {
    if (progress_bar && ui_acquire()) {
        int progress = (current_line * 100) / total_lines;
        lv_bar_set_value(progress_bar, progress, LV_ANIM_ON);
        ui_release();
    }
}

static void script_runner_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting script: %s", script_name);
    
    char full_path[128];
    // Construct path relative to assets root
    snprintf(full_path, sizeof(full_path), "storage/bad_usb_scripts/%s", script_name);

    // Set progress callback
    ducky_set_progress_callback(ducky_progress_callback);

    // For now, hardcoded to run from assets.
    // In future, this will check storage type.
    ducky_run_from_assets(full_path);
    
    // Clear callback
    ducky_set_progress_callback(NULL);

    // When done, switch back
    ui_switch_screen(SCREEN_BADUSB_BROWSER);
    script_task_handle = NULL;
    vTaskDelete(NULL);
}

static void event_handler(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_ESC) {
            if (script_task_handle) {
                ducky_abort();
                // Task will clean itself up and switch screen.
                // We might want to add a more forceful vTaskDelete if needed.
            }
        }
    }
}

void ui_badusb_running_open(void) {
    if (screen_running) lv_obj_del(screen_running);

    screen_running = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_running, lv_color_black(), 0);
    lv_obj_remove_flag(screen_running, LV_OBJ_FLAG_SCROLLABLE);

    header_ui_create(screen_running);
    footer_ui_create(screen_running);

    char display_name[56];
    // Remove extension for display
    char* dot = strrchr(script_name, '.');
    if (dot) {
        size_t len = dot - script_name;
        strncpy(display_name, script_name, len);
        display_name[len] = '\0';
    } else {
        strncpy(display_name, script_name, sizeof(display_name) -1);
    }
    
    lv_obj_t * lbl_title = lv_label_create(screen_running);
    lv_label_set_text_fmt(lbl_title, "Running: %s", display_name);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, -40);

    progress_bar = lv_bar_create(screen_running);
    lv_obj_set_size(progress_bar, 200, 20);
    lv_obj_center(progress_bar);
    lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
    
    // Make it square: apply radius 0 to both main and indicator parts
    lv_obj_set_style_radius(progress_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(progress_bar, 0, LV_PART_INDICATOR);
    
    // Optional: add a border to make it look better if it's black on black
    lv_obj_set_style_border_width(progress_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(progress_bar, lv_palette_main(LV_PALETTE_DEEP_PURPLE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(progress_bar, lv_palette_main(LV_PALETTE_DEEP_PURPLE), LV_PART_INDICATOR);

    lv_obj_t * lbl_info = lv_label_create(screen_running);
    lv_label_set_text(lbl_info, "Press BACK to cancel");
    lv_obj_align(lbl_info, LV_ALIGN_CENTER, 0, 40);

    lv_obj_add_event_cb(screen_running, event_handler, LV_EVENT_KEY, NULL);

    if (main_group) {
        lv_group_add_obj(main_group, screen_running);
        lv_group_focus_obj(screen_running);
    }

    lv_screen_load(screen_running);

    // Create the task to run the script
    xTaskCreate(script_runner_task, "script_runner", 4096, NULL, 5, &script_task_handle);
}
