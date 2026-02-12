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

static const char *TAG = "CC1101_DRIVER";
static spi_device_handle_t cc1101_spi = NULL;

void cc1101_write_burst(uint8_t reg, const uint8_t *buf, uint8_t len) {
  if (cc1101_spi == NULL) return;

  // SPI Transaction for Burst Write: [Address | Burst Bit] + [Data0] + [Data1] ...
  // Note: SPI device handles buffer management. We need to allocate a buffer that includes the address byte.

  // Max burst len check could be added here, but for frequency setting (3 bytes) it's fine.
  // Ideally we use the polling transaction for very short transfers or standard transmit for others.

  // We construct a temporary buffer: [CMD][DATA...]
  uint8_t *tx_buf = heap_caps_malloc(len + 1, MALLOC_CAP_DMA);
  if (!tx_buf) return;

  tx_buf[0] = (reg | 0x40); // Add Burst bit (0x40)
  memcpy(&tx_buf[1], buf, len);

  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.length = (len + 1) * 8;
  t.tx_buffer = tx_buf;
  t.rx_buffer = NULL;

  spi_device_transmit(cc1101_spi, &t);

  free(tx_buf);
}

/**
 * @brief Define a frequência de operação do CC1101.
 * Fórmula: Freq = (fxosc / 2^16) * FREQ[23:0]
 * Com cristal de 26MHz: FREQ = (FreqHz * 65536) / 26000000
 */
void cc1101_set_frequency(uint32_t freq_hz) {
  uint64_t freq_reg = ((uint64_t)freq_hz * 65536) / 26000000;

  uint8_t freq_bytes[3];
  freq_bytes[0] = (freq_reg >> 16) & 0xFF; // FREQ2
  freq_bytes[1] = (freq_reg >> 8) & 0xFF;  // FREQ1
  freq_bytes[2] = freq_reg & 0xFF;         // FREQ0

  // Use Burst Write starting from CC1101_FREQ2 (auto-increments to FREQ1, FREQ0)
  cc1101_write_burst(CC1101_FREQ2, freq_bytes, 3);
}

/**
 * @brief Envia um comando strobe (comando de 1 byte) para o chip.
 */
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
  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.length = 16;
  t.flags = SPI_TRANS_USE_TXDATA;
  t.tx_data[0] = reg;
  t.tx_data[1] = val;
  spi_device_transmit(cc1101_spi, &t);
}

/**
 * @brief Lê um valor de um registrador (Status ou Configuração).
 */
uint8_t cc1101_read_reg(uint8_t reg) {
  if (cc1101_spi == NULL) return 0;
  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.length = 16;
  t.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
  t.tx_data[0] = 0x80 | reg;
  t.tx_data[1] = 0x00;
  spi_device_transmit(cc1101_spi, &t);
  return t.rx_data[1];
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
  // Configuração para o driver SPI centralizado
  spi_device_config_t devcfg = {
    .clock_speed_hz = 4 * 1000 * 1000, // 4 MHz
    .mode = 0,                         // Modo SPI 0
    .cs_pin = GPIO_CS_PIN,           // Pino CS
    .queue_size = 7
  };

  // Adiciona o dispositivo ao barramento SPI (Driver Centralizado)
  esp_err_t ret = spi_add_device(SPI3_HOST, SPI_DEVICE_CC1101, &devcfg);
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
  cc1101_write_reg(CC1101_IOCFG2, 0x2E); // GDO2 em High-Impedance (Libera Pino 9)

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
}

/**
         * @brief Configura o CC1101 para modo Assíncrono (Raw Data) para Sniffer/Replay.
         * Baseado na configuração "Async" da LibSmartRC-CC1101.
         */
void cc1101_enable_async_mode(uint32_t freq_hz) {
  if (cc1101_spi == NULL) return;

  ESP_LOGI(TAG, "Configurando CC1101 para modo Async (Sniffer)...");

  cc1101_strobe(CC1101_SIDLE);
  cc1101_strobe(CC1101_SRES); // Reset para garantir estado limpo
  vTaskDelay(pdMS_TO_TICKS(5));

  // 1. Configuração de Pinos (GDO0 como Serial Data Output)
  // O ESP32 vai ler o GDO0 (GPIO 8/SDA) via RMT.
  cc1101_write_reg(CC1101_IOCFG0, 0x0D);            // 2. Configuração de Pacote (Async, Raw, Sem CRC/Manchester)
  cc1101_write_reg(CC1101_PKTCTRL0, 0x32); // Async mode, No whitening
  cc1101_write_reg(CC1101_PKTCTRL1, 0x04); // No addr check
  cc1101_write_reg(CC1101_PKTLEN,   0x00);

  // 3. Configuração RF (Frequência)
  cc1101_set_frequency(freq_hz);

  // 4. Configuração de Modem (ASK/OOK, Otimizado para 433MHz genérico)
  // Valores extraídos da LibSmartRC "RegConfigSettings" + "setCCMode"
  cc1101_write_reg(CC1101_FSCTRL1,  0x06);

  // MDMCFG4: Configuração de Bandwidth e eXponent.
  // Antes: 0x57 (~325kHz).
  // Novo: 0x3B = Bandwidth 812kHz (Abertura Máxima). 
  // Isso ajuda a pegar controles desafinados (433.80 - 434.00 MHz).
  cc1101_write_reg(CC1101_MDMCFG4, 0x3B); 
  cc1101_write_reg(CC1101_MDMCFG3, 0x93);            
  cc1101_write_reg(CC1101_MDMCFG2, 0x30); // ASK/OOK, No Sync/Preamble sense
  cc1101_write_reg(CC1101_MDMCFG1, 0x02); // FEC Off
  cc1101_write_reg(CC1101_MDMCFG0, 0xF8); // Channel Spacing

  cc1101_write_reg(CC1101_DEVIATN,  0x47); // Deviation (menos relevante pra ASK, mas mantido)

  // 5. Configuração de Ganho (AGC) - Máxima sensibilidade
  cc1101_write_reg(CC1101_AGCCTRL2, 0xC7); // MAGN_TARGET = 42dB, MAX_LNA_GAIN = 0, MAX_DVGA_GAIN = 0
  cc1101_write_reg(CC1101_AGCCTRL1, 0x00);
  cc1101_write_reg(CC1101_AGCCTRL0, 0xB2);

  // 6. Front End & Calibração
  cc1101_write_reg(CC1101_FREND1,   0x56);
  cc1101_write_reg(CC1101_FREND0,   0x11); // Usa índice 1 da PATABLE (Wait! índice 0 ou 1?)
  // Se FREND0.PA_POWER = 1, ele usa PATABLE[1].
  // Vamos usar índice 0 para simplificar.
  cc1101_write_reg(CC1101_FREND0,   0x10); // Usa PATABLE[0]

  // ESCREVENDO A TABELA DE POTÊNCIA (Crítico para alcance)
  // Para 433MHz: 0xC0 = +10dBm (Máximo)
  uint8_t pa_table[] = {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  cc1101_write_burst(CC1101_PATABLE, pa_table, 8);

  cc1101_write_reg(CC1101_FSCAL3,   0xE9);
  cc1101_write_reg(CC1101_FSCAL2,   0x2A);
  cc1101_write_reg(CC1101_FSCAL1,   0x00);
  cc1101_write_reg(CC1101_FSCAL0,   0x1F);        
  // 7. Configurações de Teste (Mágica da TI para estabilidade)
  cc1101_write_reg(CC1101_FSTEST,   0x59);
  cc1101_write_reg(CC1101_TEST2,    0x81);
  cc1101_write_reg(CC1101_TEST1,    0x35);
  cc1101_write_reg(CC1101_TEST0,    0x09);

  ESP_LOGI(TAG, "CC1101 configurado em Async Mode (GDO0 Active High)");

      // Entra em RX imediatamente
      cc1101_strobe(CC1101_SRX);
  }
  
  void cc1101_enter_tx_mode(void) {
      if (cc1101_spi == NULL) return;
      cc1101_strobe(CC1101_SIDLE);
      cc1101_strobe(CC1101_STX);
  }
  
  void cc1101_enter_rx_mode(void) {
      if (cc1101_spi == NULL) return;
      cc1101_strobe(CC1101_SIDLE);
      cc1101_strobe(CC1101_SRX);
  }
  
  void cc1101_enable_fsk_mode(uint32_t freq_hz) {
      if (cc1101_spi == NULL) return;
  
      ESP_LOGI(TAG, "Configurando CC1101 para modo FSK (Sniffer)...");
  
      cc1101_strobe(CC1101_SIDLE);
      cc1101_strobe(CC1101_SRES);
          vTaskDelay(pdMS_TO_TICKS(5));
      
          // 1. Configuração de Pinos (GDO0 como Serial Data Output)
          cc1101_write_reg(CC1101_IOCFG0, 0x0D);
      
          // 2. Pacote (Igual Async)      cc1101_write_reg(CC1101_PKTCTRL0, 0x32);
      cc1101_write_reg(CC1101_PKTCTRL1, 0x04);
      cc1101_write_reg(CC1101_PKTLEN,   0x00);
  
      // 3. RF
      cc1101_set_frequency(freq_hz);
  
      // 4. Modem (AQUI MUDA PARA FSK)
      cc1101_write_reg(CC1101_FSCTRL1,  0x06);
      
      // MDMCFG4: 0x3B = BW 812kHz
      cc1101_write_reg(CC1101_MDMCFG4, 0x3B);
      cc1101_write_reg(CC1101_MDMCFG3, 0x93); // Data Rate
  
      // MDMCFG2: FSK (0x00)
      cc1101_write_reg(CC1101_MDMCFG2, 0x00); // 2-FSK, No Sync/Preamble
      
      cc1101_write_reg(CC1101_MDMCFG1, 0x02); // FEC Off
      cc1101_write_reg(CC1101_MDMCFG0, 0xF8); // Channel Spacing
  
      cc1101_write_reg(CC1101_DEVIATN,  0x47); // Deviation +/- 47kHz
  
      // 5. AGC (Igual)
      cc1101_write_reg(CC1101_AGCCTRL2, 0xC7);
      cc1101_write_reg(CC1101_AGCCTRL1, 0x00);
      cc1101_write_reg(CC1101_AGCCTRL0, 0xB2);
  
      // 6. Front End (Igual)
      cc1101_write_reg(CC1101_FREND1,   0x56);
      cc1101_write_reg(CC1101_FREND0,   0x10);
  
      uint8_t pa_table[] = {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
      cc1101_write_burst(CC1101_PATABLE, pa_table, 8);
  
      cc1101_write_reg(CC1101_FSCAL3,   0xE9);
      cc1101_write_reg(CC1101_FSCAL2,   0x2A);
      cc1101_write_reg(CC1101_FSCAL1,   0x00);
      cc1101_write_reg(CC1101_FSCAL0,   0x1F);
  
      // 7. Test (Igual)
      cc1101_write_reg(CC1101_FSTEST,   0x59);
      cc1101_write_reg(CC1101_TEST2,    0x81);
      cc1101_write_reg(CC1101_TEST1,    0x35);
      cc1101_write_reg(CC1101_TEST0,    0x09);
  
      ESP_LOGI(TAG, "CC1101 configurado em FSK Mode (GDO0 Active High)");
  
      cc1101_strobe(CC1101_SRX);
  }
  
