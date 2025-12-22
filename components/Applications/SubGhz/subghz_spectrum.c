#include "subghz_spectrum.h"
#include "cc1101.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <rom/ets_sys.h>

static const char *TAG = "SUBGHZ_SPECTRUM";
static TaskHandle_t spectrum_task_handle = NULL;

float spectrum_data[SPECTRUM_SAMPLES];

void subghz_spectrum_task(void *pvParameters) {
  int log_counter = 0;

  // Configuração de "Zoom Out": Varredura de 600 kHz (+/- 300 kHz)
  // Focada em 433.92 MHz para maior sensibilidade
  // Centro: 433.92 MHz
  // Range: 600 kHz / 80 samples = 7.5 kHz por passo (Alta resolução)
  const uint32_t CENTER_FREQ = 433920000;
  const uint32_t SPAN_HZ = 600000; 
  const uint32_t STEP_HZ = SPAN_HZ / SPECTRUM_SAMPLES;
  const uint32_t START_FREQ = CENTER_FREQ - (SPAN_HZ / 2);

  while (1) {
    // We assume the driver is initialized. If not, we might want to check or handle it.
    // There is no public "is_initialized" in cc1101.h, but we rely on the system setup.

    float sweep_max_dbm = -130.0; // Rastreia o pico máximo desta varredura completa

    // Loop de varredura (Core Loop)
    // Executa 80 iterações com delay correto para calibração
    for (int i = 0; i < SPECTRUM_SAMPLES; i++) {
      uint32_t current_freq = START_FREQ + (i * STEP_HZ);

      // 1. IDLE para preparar
      cc1101_strobe(CC1101_SIDLE);

      // 2. Define a nova frequência
      cc1101_set_frequency(current_freq);

      // 3. Ativa RX (Calibração automática na transição IDLE->RX)
      cc1101_strobe(CC1101_SRX);

      // Tempo de estabilização otimizado para pequenos saltos (800us)
      ets_delay_us(800); 

      // --- DETECÇÃO DE PICO (OOK MITIGATION) ---
      // Lê o RSSI múltiplas vezes para pegar o sinal "ON" se estiver pulsando (controle remoto)
      float local_max = -130.0;
      for(int k=0; k<5; k++) {
        uint8_t raw = cc1101_read_reg(CC1101_RSSI | 0x40);
        float val = cc1101_convert_rssi(raw);
        if(val > local_max) local_max = val;
        ets_delay_us(50); // Breve intervalo entre amostras
      }

      spectrum_data[i] = local_max;

      // Atualiza o máximo global da varredura
      if(local_max > sweep_max_dbm) {
        sweep_max_dbm = local_max;
      }

      // Yield a cada 4 amostras para não travar a UI
      if (i % 4 == 0) {
        vTaskDelay(1);
      }
    }

    if (++log_counter >= 5) { 
      log_counter = 0;
    }

    // Delay entre varreduras para aliviar o barramento SPI e a CPU
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void subghz_spectrum_start(void) {
  if (spectrum_task_handle != NULL) {
    ESP_LOGW(TAG, "Spectrum task already running");
    return;
  }

  // Inicializa o array de espectro com "noise floor"
  for(int i = 0; i < SPECTRUM_SAMPLES; i++) {
    spectrum_data[i] = -110.0;
  }

  ESP_LOGI(TAG, "Starting Spectrum Task...");
  xTaskCreatePinnedToCore(subghz_spectrum_task, "subghz_scan", 4096, NULL, 1, &spectrum_task_handle, 1);
}

void subghz_spectrum_stop(void) {
  if (spectrum_task_handle != NULL) {
    ESP_LOGI(TAG, "Stopping Spectrum Task...");
    vTaskDelete(spectrum_task_handle);
    spectrum_task_handle = NULL;
  }
}

