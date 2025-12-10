#include "wifi_scan_ui.h"
#include "misc/lv_palette.h"
#include "ui_manager.h"
#include "esp_log.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "wifi_service.h"
#include "freertos/task.h"

// Importe seu serviço de Wi-Fi aqui para ter acesso ao wifi_service_scan()
// #include "wifi_service.h"

static const char *TAG = "UI_SCAN";

static lv_obj_t * screen_scan = NULL;
static lv_obj_t * spinner = NULL;
static lv_obj_t * lbl_status = NULL;

// --- TASK DO WORKER (RODA EM PARALELO) ---
// Essa task faz o trabalho sujo sem travar a animação
static void scan_worker_task(void *arg)
{
    ESP_LOGI(TAG, "Iniciando Scan em Background...");

    // 1. Executa o Scan Bloqueante (backend)
    // O LVGL continua rodando liso na outra task enquanto isso aqui espera
    wifi_service_scan();
    ESP_LOGI(TAG, "Scan finalizado pelo Backend.");

    // 2. Atualiza a UI para Sucesso (Thread-Safe!)
    if (ui_acquire()) {
        if (spinner) {
            lv_obj_del(spinner); // Remove a animação
            spinner = NULL;
        }

        if (lbl_status) {
            lv_label_set_text(lbl_status, "Scan Concluido!");
            lv_obj_set_style_text_color(lbl_status, lv_palette_main(LV_PALETTE_GREEN), 0);
            
            // Re-centraliza o texto
            lv_obj_align(lbl_status, LV_ALIGN_CENTER, 0, 0);
            
            // Adiciona um ícone de check se quiser
            lv_obj_t * icon = lv_label_create(screen_scan);
            lv_label_set_text(icon, LV_SYMBOL_OK);
            lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(icon, lv_palette_main(LV_PALETTE_GREEN), 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, -30);
        }
        
        ui_release();
    }

    // 3. Aguarda um pouco para o usuário ver a mensagem verde
    vTaskDelay(pdMS_TO_TICKS(1500));

    // 4. Volta para o Menu (ou para a Lista de Resultados)
    ESP_LOGI(TAG, "Retornando...");
    
    // Use ui_switch_screen para garantir limpeza de memória
    // Nota: Como estamos numa task separada, o switch screen cuida do Mutex
    ui_switch_screen(SCREEN_WIFI_MENU); 
    // OU: ui_switch_screen(SCREEN_WIFI_LIST); se você já tiver a tela de lista

    // 5. A task se autodestrói
    vTaskDelete(NULL);
}

// --- CONSTRUÇÃO DA TELA ---
void ui_wifi_scan_open(void)
{
    if (screen_scan) {
        lv_obj_del(screen_scan);
        screen_scan = NULL;
    }

    // 1. Cria a tela preta
    screen_scan = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_scan, lv_color_black(), 0);

    // 2. Cria o Spinner (Animação de Carga)
    // 1000ms para uma volta, arco de 60 graus
    spinner = lv_spinner_create(screen_scan);
    lv_obj_set_size(spinner, 80, 80);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_arc_color(spinner, lv_palette_main(LV_PALETTE_DEEP_PURPLE), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x404040), LV_PART_MAIN);

    // 3. Texto de Status
    lbl_status = lv_label_create(screen_scan);
    lv_label_set_text(lbl_status, "Scanning...");
    lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_status, lv_color_white(), 0);
    lv_obj_align(lbl_status, LV_ALIGN_CENTER, 0, 40);

    // Carrega a tela
    lv_screen_load(screen_scan);

    // 4. Cria a Task que vai rodar o Wifi Scan
    // É importante criar DEPOIS de carregar a tela para o usuário ver o spinner rodando
    xTaskCreate(scan_worker_task, "WifiScanWorker", 4096, NULL, 5, NULL);
}
