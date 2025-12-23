// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "ram_monitor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"

static const char *TAG = "RAM_MONITOR";

#define MONITOR_INTERVAL_MS 5000 
#define STACK_SIZE_BYTES    3072 

static void ram_monitor_task(void *pvParameters) {
  ESP_LOGI(TAG, "Monitoramento de RAM iniciado.");

  while (1) {
    uint32_t free_heap = esp_get_free_heap_size();

    uint32_t min_free_heap = esp_get_minimum_free_heap_size();

    uint32_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    uint32_t spiram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    ESP_LOGI(TAG, "------------------------------------------------");
    ESP_LOGI(TAG, "RAM STATUS (Bytes):");
    ESP_LOGI(TAG, "Total Free    : %lu", (unsigned long)free_heap);
    ESP_LOGI(TAG, "Internal Free : %lu", (unsigned long)internal_free);

    if (spiram_free > 0) {
      ESP_LOGI(TAG, "PSRAM Free    : %lu", (unsigned long)spiram_free);
    }

    ESP_LOGI(TAG, "Min. Ever Free: %lu (Watermark)", (unsigned long)min_free_heap);
    ESP_LOGI(TAG, "------------------------------------------------");

    vTaskDelay(pdMS_TO_TICKS(MONITOR_INTERVAL_MS));
  }
}

// Função pública para iniciar a task
void ram_monitor(void) {
  xTaskCreatePinnedToCore(
    ram_monitor_task,   
    "RamMonitor",       
    STACK_SIZE_BYTES,   
    NULL,               
    1,                  
    NULL,               
    1                   
  );
}
