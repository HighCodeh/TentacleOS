// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#include "cc1101.h"
#include "spi.h"
#include "pin_def.h"
#include <string.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <rom/ets_sys.h>

static const char *TAG = "CC1101_driver";
static spi_device_handle_t cc1101_spi = NULL;

float spectrum_data[SPECTRUM_SAMPLES]; 

void cc1101_set_frequency(uint32_t freq_hz) {
  uint64_t freq_reg = ((uint64_t)freq_hz * 65536) / 26000000;

  cc1101_write_reg(CC1101_FREQ2, (freq_reg >> 16) & 0xFF);
  cc1101_write_reg(CC1101_FREQ1, (freq_reg >> 8) & 0xFF);
  cc1101_write_reg(CC1101_FREQ0, freq_reg & 0xFF);
}

void cc1101_strobe(uint8_t cmd) {
  // ... (código existente mantido)
  if (cc1101_spi == NULL) return;
  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.length = 8;
  t.flags = SPI_TRANS_USE_TXDATA;
  t.tx_data[0] = cmd;
  spi_device_transmit(cc1101_spi, &t);
}

/**
 * @brief Escreve um valor em um registrador de configuração.
 */
void cc1101_write_reg(uint8_t reg, uint8_t val) {
  if (cc1101_spi == NULL) return;
  uint8_t tx_data[2] = {reg, val};
  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.length = 16;
  t.tx_buffer = tx_data;
  spi_device_transmit(cc1101_spi, &t);
}

/**
 * @brief Lê um valor de um registrador (Status ou Configuração).
 */
uint8_t cc1101_read_reg(uint8_t reg) {
  if (cc1101_spi == NULL) return 0;
  uint8_t tx_data[2] = {0x80 | reg, 0x00};
  uint8_t rx_data[2] = {0x00, 0x00};
  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.length = 16;
  t.tx_buffer = tx_data;
  t.rx_buffer = rx_data;
  spi_device_transmit(cc1101_spi, &t);
  return rx_data[1];
}

/**
 * @brief Converte o valor bruto do registrador RSSI para dBm.
 */
float cc1101_convert_rssi(uint8_t rssi_raw) {
  float rssi_dbm;
  if (rssi_raw >= 128) {
    rssi_dbm = ((float)(rssi_raw - 256) / 2.0) - 74.0;
  } else {
    rssi_dbm = ((float)rssi_raw / 2.0) - 74.0;
  }
  return rssi_dbm;
}

/**
 * @brief Inicializa o dispositivo CC1101 no barramento SPI.
 */
void cc1101_init(void) {
  // Inicializa o array de espectro com "noise floor"
  for(int i = 0; i < SPECTRUM_SAMPLES; i++) {
    spectrum_data[i] = -110.0;
  }

  // Configuração para o driver SPI centralizado
  spi_device_config_t devcfg = {
    .clock_speed_hz = 4 * 1000 * 1000, // 4 MHz
    .mode = 0,                         // Modo SPI 0
    .cs_pin = GPIO_CS_PIN,           // Pino CS
    .queue_size = 7
  };

  // Adiciona o dispositivo ao barramento SPI (Driver Centralizado)
  esp_err_t ret = spi_add_device(SPI_DEVICE_CC1101, &devcfg);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Falha ao adicionar dispositivo SPI: %s", esp_err_to_name(ret));
    return;
  }

  // Obtém o handle do dispositivo
  cc1101_spi = spi_get_handle(SPI_DEVICE_CC1101);

  // Reset via comando SRES
  cc1101_strobe(CC1101_SRES);
  vTaskDelay(pdMS_TO_TICKS(50));

  // --- CORREÇÃO DE CONFLITO DE PINS ---
  // Configura GDO0 e GDO2 para High-Impedance (0x2E)
  cc1101_write_reg(CC1101_IOCFG0, 0x2E); 
  cc1101_write_reg(CC1101_IOCFG2, 0x2E);

  // --- TESTE DE COMUNICAÇÃO ---
  uint8_t version = cc1101_read_reg(CC1101_VERSION | 0x40);
  if (version == 0x00 || version == 0xFF) {
    ESP_LOGE(TAG, "CC1101 não detectado! Cheque MISO/MOSI/SCLK/CS");
  } else {
    ESP_LOGI(TAG, "CC1101 Detectado! Versão do Chip: 0x%02X", version);
  }

  // Configuração para modo Scanner
  cc1101_write_reg(CC1101_FSCTRL1, 0x06);
  cc1101_write_reg(CC1101_MDMCFG4, 0x85); // BW = 203kHz (Mais largo para capturar sinais instáveis)
  cc1101_write_reg(CC1101_MDMCFG2, 0x30); 
  cc1101_write_reg(CC1101_MCSM0, 0x18);   
  cc1101_write_reg(CC1101_AGCCTRL2, 0x07); // AGC Max Gain
  cc1101_write_reg(CC1101_AGCCTRL1, 0x00);
  cc1101_write_reg(CC1101_AGCCTRL0, 0x91);

  cc1101_set_frequency(433920000);

  cc1101_strobe(CC1101_SRX);
  ESP_LOGI(TAG, "Hardware pronto. Iniciando Scanner...");

  // Inicia a task de varredura com prioridade BAIXA (1) para não travar a UI/IDLE
  xTaskCreate(cc1101_spectrum_task, "cc1101_scan", 4096, NULL, 1, NULL);
}

/**
     * @brief Task de varredura (Sweep) de frequência para o Analisador de Espectro.
     */
void cc1101_spectrum_task(void *pvParameters) {
  int log_counter = 0;

  // Focada em 433.92 MHz para maior sensibilidade
  // Centro: 433.92 MHz
  // Range: 600 kHz / 80 samples = 7.5 kHz por passo (Alta resolução)
  const uint32_t CENTER_FREQ = 433920000;
  const uint32_t SPAN_HZ = 600000; 
  const uint32_t STEP_HZ = SPAN_HZ / SPECTRUM_SAMPLES;
  const uint32_t START_FREQ = CENTER_FREQ - (SPAN_HZ / 2);

  while (1) {
    if (cc1101_spi == NULL) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

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

      if (i % 10 == 0) {
        vTaskDelay(pdMS_TO_TICKS(1));
      }
    }

    if (++log_counter >= 5) { 
      log_counter = 0;
    }        
  }
}

