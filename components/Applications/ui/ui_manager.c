#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "ui_manager.h"
#include "esp_timer.h"
#include "home_ui.h"
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

void ui_init(void)
{
    ESP_LOGI(TAG, "Inicializando UI Manager...");

    xGuiSemaphore = xSemaphoreCreateMutex();
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
        // Espera indefinidamente até conseguir o mutex
        return (xSemaphoreTake(xGuiSemaphore, portMAX_DELAY) == pdTRUE);
    }
    return false;
}

void ui_release(void)
{
    if (xGuiSemaphore != NULL) {
        xSemaphoreGive(xGuiSemaphore);
    }
}
