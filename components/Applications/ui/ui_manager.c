#include "core/lv_group.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "ui_manager.h"
#include "wifi_service.h"
#include "esp_timer.h"
#include "home_ui.h"
#include "menu_ui.h"
#include "wifi_ui.h"
#include "wifi_scan_ui.h"
#include "subghz_spectrum_ui.h"
#include "esp_log.h"

#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"



// Inclua aqui a tela inicial

#define TAG "UI_MANAGER"

#define UI_TASK_STACK_SIZE      (4096 * 2) 
#define UI_TASK_PRIORITY        (tskIDLE_PRIORITY + 2) 
#define UI_TASK_CORE            1 

#define LVGL_TICK_PERIOD_MS     5

static SemaphoreHandle_t xGuiSemaphore = NULL;

static void ui_task(void *pvParameter);
static void lv_tick_task(void *arg);
static void clear_current_screen(void);

screen_id_t current_screen_id = SCREEN_NONE;

void ui_init(void)
{
    ESP_LOGI(TAG, "Inicializando UI Manager...");

    xGuiSemaphore = xSemaphoreCreateRecursiveMutex();
    if (!xGuiSemaphore) {
        ESP_LOGE(TAG, "Falha ao criar Mutex da UI");
        return;
    }

    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &lv_tick_task,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, LVGL_TICK_PERIOD_MS * 1000));

    xTaskCreatePinnedToCore(
        ui_task,            
        "UI Task",          
        UI_TASK_STACK_SIZE, 
        NULL,               
        UI_TASK_PRIORITY,   
        NULL,               
        UI_TASK_CORE        
    );
    
    ESP_LOGI(TAG, "UI Manager inicializado com sucesso.");
}

static void ui_task(void *pvParameter)
{
    ESP_LOGI(TAG, "UI Task iniciada.");

    if (ui_acquire()) {
        ui_home_open(); 
        ui_release();
    }

    while (1) {
        if (ui_acquire()) {
            lv_timer_handler();
            ui_release();
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void clear_current_screen(void){
  lv_group_remove_all_objs(main_group);
}

void ui_switch_screen(screen_id_t new_screen) {
  if (ui_acquire()) {

    clear_current_screen();

    switch (new_screen) {
      case SCREEN_HOME:
        ui_home_open();
        break;

      case SCREEN_MENU:
        ui_menu_open();
        break;

      case SCREEN_WIFI_MENU:
        ui_wifi_menu_open();
        break;
      
      case SCREEN_WIFI_SCAN:
        ui_wifi_scan_open();
        break;

      case SCREEN_SUBGHZ_SPECTRUM:
        ui_subghz_spectrum_open();
        break;

      default:
        break;
    }

    current_screen_id = new_screen;
    ui_release();
  }
}

// Callback do Timer de Hardware (Interrupção)
static void lv_tick_task(void *arg)
{
    (void) arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

// --- Funções Públicas de Thread-Safety ---

bool ui_acquire(void)
{
  if (xGuiSemaphore != NULL) {
        // MUDANÇA: xSemaphoreTake -> xSemaphoreTakeRecursive
        return (xSemaphoreTakeRecursive(xGuiSemaphore, portMAX_DELAY) == pdTRUE);
    }
    return false;
}

void ui_release(void)
{
  if (xGuiSemaphore != NULL) {
        // MUDANÇA: xSemaphoreGive -> xSemaphoreGiveRecursive
        xSemaphoreGiveRecursive(xGuiSemaphore);
    }
}


