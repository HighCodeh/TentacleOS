#include "ui_badusb_connect.h"
#include "ui_manager.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "lv_port_indev.h"
#include "esp_log.h"
#include "bad_usb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "UI_BADUSB_CONNECT";
static lv_obj_t * screen_connect = NULL;
static lv_obj_t * spinner = NULL;
static TaskHandle_t waiter_task = NULL;

static void connection_waiter_task(void *pvParameters) {
    // Chama a função bloqueante original que o usuário pediu
    bad_usb_wait_for_connection(); 
    
    // Ao retornar, já esperou a conexão e o delay de 2s.
    // Troca para a tela de execução com segurança de thread
    ui_switch_screen(SCREEN_BADUSB_RUNNING);
    
    waiter_task = NULL;
    vTaskDelete(NULL);
}

static void event_handler(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            // Se o usuário cancelar, mata a task de espera
            if (waiter_task) {
                vTaskDelete(waiter_task);
                waiter_task = NULL;
            }
            ui_switch_screen(SCREEN_BADUSB_BROWSER);
        }
    }
}

void ui_badusb_connect_open(void) {
    if (screen_connect) lv_obj_del(screen_connect);

    screen_connect = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_connect, lv_color_black(), 0);
    lv_obj_remove_flag(screen_connect, LV_OBJ_FLAG_SCROLLABLE);

    header_ui_create(screen_connect);

    // Spinner Roxo (Deep Purple)
    spinner = lv_spinner_create(screen_connect);
    lv_obj_set_size(spinner, 50, 50);
    lv_obj_center(spinner);
    lv_obj_set_style_arc_color(spinner, lv_palette_main(LV_PALETTE_DEEP_PURPLE), LV_PART_INDICATOR);

    lv_obj_t * lbl_status = lv_label_create(screen_connect);
    lv_label_set_text(lbl_status, "Waiting for USB...");
    lv_obj_set_style_text_color(lbl_status, lv_color_white(), 0);
    lv_obj_align(lbl_status, LV_ALIGN_CENTER, 0, 50);

    lv_obj_t * lbl_hint = lv_label_create(screen_connect);
    lv_label_set_text(lbl_hint, "Connect to PC now");
    lv_obj_set_style_text_font(lbl_hint, &lv_font_montserrat_12, 0); 
    lv_obj_set_style_text_color(lbl_hint, lv_color_white(), 0);
    lv_obj_align(lbl_hint, LV_ALIGN_CENTER, 0, 70);

    footer_ui_create(screen_connect);

    lv_obj_add_event_cb(screen_connect, event_handler, LV_EVENT_KEY, NULL);

    if (main_group) {
        lv_group_add_obj(main_group, screen_connect);
        lv_group_focus_obj(screen_connect);
    }

    lv_screen_load(screen_connect);

    // Inicia a task que vai bloquear esperando o USB
    xTaskCreate(connection_waiter_task, "usb_waiter", 2048, NULL, 5, &waiter_task);
}
