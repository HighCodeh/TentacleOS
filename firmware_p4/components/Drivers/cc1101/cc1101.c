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
#include <stdbool.h>
#include <math.h>

static const char *TAG = "CC1101_DRIVER";
static spi_device_handle_t cc1101_spi = NULL;

static float s_current_freq_mhz = 433.92;
static uint8_t s_current_modulation = 2; // Default ASK/OOK
static int s_current_pa_dbm = 10; 
static uint8_t s_active_preset_id = 0;

static const uint8_t PA_TABLE_315[8] = {0x12, 0x0D, 0x1C, 0x34, 0x51, 0x85, 0xCB, 0xC2}; // 300 - 348 MHz
static const uint8_t PA_TABLE_433[8] = {0x12, 0x0E, 0x1D, 0x34, 0x60, 0x84, 0xC8, 0xC0}; // 387 - 464 MHz
static const uint8_t PA_TABLE_868[8] = {0x03, 0x17, 0x1D, 0x26, 0x37, 0x50, 0x86, 0xCD}; // 779 - 899 MHz
static const uint8_t PA_TABLE_915[8] = {0x03, 0x0E, 0x1E, 0x27, 0x38, 0x8E, 0x84, 0xCC}; // 900 - 928 MHz

void cc1101_write_burst(uint8_t reg, const uint8_t *buf, uint8_t len) {
  if (cc1101_spi == NULL) return;

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

uint8_t cc1101_read_reg(uint8_t reg) {
  if (cc1101_spi == NULL) return 0;
  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.length = 16;
  t.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
  t.tx_data[0] = 0x80 | reg; // Read bit
  t.tx_data[1] = 0x00;
  spi_device_transmit(cc1101_spi, &t);
  return t.rx_data[1];
}

void cc1101_read_burst(uint8_t reg, uint8_t *buf, uint8_t len) {
  if (cc1101_spi == NULL || len == 0) return;

  uint8_t *tx_buf = heap_caps_malloc(len + 1, MALLOC_CAP_DMA);
  uint8_t *rx_buf = heap_caps_malloc(len + 1, MALLOC_CAP_DMA);
  if (!tx_buf || !rx_buf) {
    if (tx_buf) free(tx_buf);
    if (rx_buf) free(rx_buf);
    return;
  }

  memset(tx_buf, 0, len + 1);
  tx_buf[0] = (reg | 0xC0); // Read (0x80) | Burst (0x40)

  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.length = (len + 1) * 8;
  t.tx_buffer = tx_buf;
  t.rx_buffer = rx_buf;

  spi_device_transmit(cc1101_spi, &t);

  memcpy(buf, &rx_buf[1], len);

  free(tx_buf);
  free(rx_buf);
}

void cc1101_strobe(uint8_t cmd) {
  if (cc1101_spi == NULL) return;
  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.length = 8;
  t.flags = SPI_TRANS_USE_TXDATA;
  t.tx_data[0] = cmd;
  spi_device_transmit(cc1101_spi, &t);
}


// Frequency & Calibration
static long map(long x, long in_min, long in_max, long out_min, long out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void cc1101_calibrate(void) {
  uint8_t fsctrl0 = 0;
  uint8_t test0 = 0x09;
  bool needs_fscal2_adj = false;

  if (s_current_freq_mhz >= 300 && s_current_freq_mhz <= 348) {
    fsctrl0 = (uint8_t)map(s_current_freq_mhz * 1000, 300000, 348000, 24, 28);
    if (s_current_freq_mhz < 322.88) test0 = 0x0B;
    else needs_fscal2_adj = true;
  } else if (s_current_freq_mhz >= 378 && s_current_freq_mhz <= 464) {
    fsctrl0 = (uint8_t)map(s_current_freq_mhz * 1000, 378000, 464000, 31, 38);
    if (s_current_freq_mhz < 430.5) test0 = 0x0B;
    else needs_fscal2_adj = true;
  } else if (s_current_freq_mhz >= 779 && s_current_freq_mhz <= 899.99) {
    fsctrl0 = (uint8_t)map(s_current_freq_mhz * 1000, 779000, 899000, 65, 76);
    if (s_current_freq_mhz < 861) test0 = 0x0B;
    else needs_fscal2_adj = true;
  } else if (s_current_freq_mhz >= 900 && s_current_freq_mhz <= 928) {
    fsctrl0 = (uint8_t)map(s_current_freq_mhz * 1000, 900000, 928000, 77, 79);
    needs_fscal2_adj = true;
  }

  cc1101_write_reg(CC1101_FSCTRL0, fsctrl0);
  cc1101_write_reg(CC1101_TEST0, test0);

  cc1101_strobe(CC1101_SIDLE);
  cc1101_strobe(CC1101_SCAL);

  // Wait for calibration (check MARCSTATE or just wait)
  vTaskDelay(pdMS_TO_TICKS(5));

  if (needs_fscal2_adj) {
    uint8_t s = cc1101_read_reg(CC1101_FSCAL2 | 0x40);
    if (s < 32) cc1101_write_reg(CC1101_FSCAL2, s + 32);
  }
}

void cc1101_set_frequency(uint32_t freq_hz) {
  s_current_freq_mhz = (float)freq_hz / 1000000.0;

  // Standard frequency calculation for 26MHz XTAL
  // FREQ = (f_carrier * 2^16) / 26,000,000
  uint64_t freq_reg = ((uint64_t)freq_hz * 65536) / 26000000;

  uint8_t freq_bytes[3];
  freq_bytes[0] = (freq_reg >> 16) & 0xFF; // FREQ2
  freq_bytes[1] = (freq_reg >> 8) & 0xFF;  // FREQ1
  freq_bytes[2] = freq_reg & 0xFF;         // FREQ0

  cc1101_write_burst(CC1101_FREQ2, freq_bytes, 3);

  cc1101_calibrate();
}

// Modem Configuration
void cc1101_set_rx_bandwidth(float khz) {
  uint8_t mdmcfg4 = cc1101_read_reg(CC1101_MDMCFG4);
  uint8_t drate_e = mdmcfg4 & 0x0F; // Preserve Data Rate Exponent

  int s1 = 3;
  int s2 = 3;
  float f = khz;

  for (int i = 0; i < 3; i++) {
    if (f > 101.5625) { f /= 2; s1--; }
    else { i = 3; }
  }
  for (int i = 0; i < 3; i++) {
    if (f > 58.1) { f /= 1.25; s2--; }
    else { i = 3; }
  }

  s1 *= 64;
  s2 *= 16;
  uint8_t rxbw_val = s1 + s2;

  cc1101_write_reg(CC1101_MDMCFG4, rxbw_val | drate_e);
}

void cc1101_set_data_rate(float baud) {
  uint8_t mdmcfg4 = cc1101_read_reg(CC1101_MDMCFG4);
  uint8_t rxbw_val = mdmcfg4 & 0xF0; // Preserve RX Bandwidth

  float c = baud;
  uint8_t mdmcfg3 = 0;
  uint8_t drate_e = 0;

  if (c > 1621.83) c = 1621.83;
  if (c < 0.0247955) c = 0.0247955;

  for (int i = 0; i < 20; i++) {
    if (c <= 0.0494942) {
      c = c - 0.0247955;
      c = c / 0.00009685;
      mdmcfg3 = (uint8_t)c;
      float s1 = (c - mdmcfg3) * 10;
      if (s1 >= 5) mdmcfg3++;
      i = 20;
    } else {
      drate_e++;
      c = c / 2;
    }
  }

  cc1101_write_reg(CC1101_MDMCFG4, rxbw_val | drate_e);
  cc1101_write_reg(CC1101_MDMCFG3, mdmcfg3);
}

void cc1101_set_deviation(float dev) {
  float f = 1.586914;
  float v = 0.19836425;
  int c = 0;

  if (dev > 380.859375) dev = 380.859375;
  if (dev < 1.586914) dev = 1.586914;

  for (int i = 0; i < 255; i++) {
    f += v;
    if (c == 7) { v *= 2; c = -1; i += 8; }
    if (f >= dev) { c = i; i = 255; }
    c++;
  }

  cc1101_write_reg(CC1101_DEVIATN, (uint8_t)c);
}

void cc1101_set_modulation(uint8_t modulation) {
  // 0=2-FSK, 1=GFSK, 2=ASK/OOK, 3=4-FSK, 4=MSK
  if (modulation > 4) modulation = 4;
  s_current_modulation = modulation;

  uint8_t m2modfm = 0;
  uint8_t frend0 = 0;

  switch (modulation) {
    case 0: m2modfm = 0x00; frend0 = 0x10; break; // 2-FSK
    case 1: m2modfm = 0x10; frend0 = 0x10; break; // GFSK
    case 2: m2modfm = 0x30; frend0 = 0x11; break; // ASK/OOK
    case 3: m2modfm = 0x40; frend0 = 0x10; break; // 4-FSK
    case 4: m2modfm = 0x70; frend0 = 0x10; break; // MSK
  }

  uint8_t current_mdmcfg2 = cc1101_read_reg(CC1101_MDMCFG2);
  uint8_t dcoff = current_mdmcfg2 & 0x80;
  uint8_t manch = current_mdmcfg2 & 0x08;
  uint8_t syncm = current_mdmcfg2 & 0x07;

  cc1101_write_reg(CC1101_MDMCFG2, dcoff | m2modfm | manch | syncm);
  cc1101_write_reg(CC1101_FREND0, frend0);

  // Re-apply PA setting because ASK/OOK uses different PATABLE index logic in SmartRC
  cc1101_set_pa(s_current_pa_dbm);
}

void cc1101_set_pa(int dbm) {
  s_current_pa_dbm = dbm;
  uint8_t pa_val = 0xC0; // Default max

  // Select based on frequency band and dbm
  const uint8_t *table = PA_TABLE_433; // Default
  if (s_current_freq_mhz >= 300 && s_current_freq_mhz <= 348) table = PA_TABLE_315;
  else if (s_current_freq_mhz >= 779 && s_current_freq_mhz <= 899.99) table = PA_TABLE_868;
  else if (s_current_freq_mhz >= 900 && s_current_freq_mhz <= 928) table = PA_TABLE_915;

  if (dbm <= -30) pa_val = table[0];
  else if (dbm <= -20) pa_val = table[1];
  else if (dbm <= -15) pa_val = table[2];
  else if (dbm <= -10) pa_val = table[3];
  else if (dbm <= 0)   pa_val = table[4]; 
  else if (dbm <= 5)   pa_val = table[5];
  else if (dbm <= 7)   pa_val = table[6];
  else pa_val = table[7];

  uint8_t pa_table[8] = {0};

  if (s_current_modulation == 2) { // ASK/OOK
    pa_table[0] = 0x00;
    pa_table[1] = pa_val;
  } else {
    pa_table[0] = pa_val;
    pa_table[1] = 0x00;
  }

  cc1101_write_burst(CC1101_PATABLE, pa_table, 8);
}

void cc1101_set_channel(uint8_t channel) {
  cc1101_write_reg(CC1101_CHANNR, channel);
}

void cc1101_set_chsp(float khz) {
  float f = khz;
  uint8_t mdmcfg0 = 0;
  uint8_t mdmcfg1 = cc1101_read_reg(CC1101_MDMCFG1) & 0xFC; // Clear CHSP (lower 2 bits)
  uint8_t chsp_e = 0;

  if (f > 405.456543) f = 405.456543;
  if (f < 25.390625) f = 25.390625;

  for (int i = 0; i < 5; i++) {
    if (f <= 50.682068) {
      f -= 25.390625;
      f /= 0.0991825;
      mdmcfg0 = (uint8_t)f;
      float s1 = (f - mdmcfg0) * 10;
      if (s1 >= 5) mdmcfg0++;
      i = 5;
    } else {
      chsp_e++;
      f /= 2;
    }
  }

  cc1101_write_reg(CC1101_MDMCFG1, mdmcfg1 | chsp_e);
  cc1101_write_reg(CC1101_MDMCFG0, mdmcfg0);
}

void cc1101_set_sync_mode(uint8_t mode) {
  if (mode > 7) mode = 7;
  uint8_t mdmcfg2 = cc1101_read_reg(CC1101_MDMCFG2);
  mdmcfg2 &= 0xF8; // Clear SYNC_MODE (lower 3 bits)
  mdmcfg2 |= mode;
  cc1101_write_reg(CC1101_MDMCFG2, mdmcfg2);
}

void cc1101_set_fec(bool enable) {
  uint8_t mdmcfg1 = cc1101_read_reg(CC1101_MDMCFG1);
  if (enable) mdmcfg1 |= 0x80;
  else mdmcfg1 &= 0x7F;
  cc1101_write_reg(CC1101_MDMCFG1, mdmcfg1);
}

void cc1101_set_preamble(uint8_t preamble_bytes) {
  uint8_t val = 0;
  // Map bytes to register value (approximate)
  if (preamble_bytes <= 2) val = 0;
  else if (preamble_bytes <= 3) val = 1;
  else if (preamble_bytes <= 4) val = 2;
  else if (preamble_bytes <= 6) val = 3;
  else if (preamble_bytes <= 8) val = 4;
  else if (preamble_bytes <= 12) val = 5;
  else if (preamble_bytes <= 16) val = 6;
  else val = 7;

  uint8_t mdmcfg1 = cc1101_read_reg(CC1101_MDMCFG1);
  mdmcfg1 &= 0x8F; // Clear NUM_PREAMBLE (bits 4-6)
  mdmcfg1 |= (val << 4);
  cc1101_write_reg(CC1101_MDMCFG1, mdmcfg1);
}

void cc1101_set_dc_filter_off(bool disable) {
  uint8_t mdmcfg2 = cc1101_read_reg(CC1101_MDMCFG2);
  if (disable) mdmcfg2 |= 0x80; // Disable DC blocking
  else mdmcfg2 &= 0x7F; // Enable DC blocking
  cc1101_write_reg(CC1101_MDMCFG2, mdmcfg2);
}

void cc1101_set_manchester(bool enable) {
  uint8_t mdmcfg2 = cc1101_read_reg(CC1101_MDMCFG2);
  if (enable) mdmcfg2 |= 0x08;
  else mdmcfg2 &= 0xF7;
  cc1101_write_reg(CC1101_MDMCFG2, mdmcfg2);
}


void cc1101_send_data(const uint8_t *data, size_t len) {
  if (cc1101_spi == NULL || len == 0) return;
  if (len > 61) len = 61;

  cc1101_strobe(CC1101_SIDLE);
  cc1101_strobe(CC1101_SFTX); // Flush TX FIFO

  cc1101_write_reg(CC1101_TXFIFO, (uint8_t)len);
  cc1101_write_burst(CC1101_TXFIFO, data, (uint8_t)len);

  cc1101_strobe(CC1101_STX);

  // Wait for transmission to complete (checking MARCSTATE or GDO if configured)
  // For now, a simple delay or polling MARCSTATE
  uint8_t marcstate;
  do {
    marcstate = cc1101_read_reg(CC1101_MARCSTATE | 0x40) & 0x1F;
  } while (marcstate == 0x13 || marcstate == 0x14 || marcstate == 0x15); // TX/TX_END states
}

float cc1101_convert_rssi(uint8_t rssi_raw) {
  float rssi_dbm;
  if (rssi_raw >= 128) {
    rssi_dbm = ((float)(rssi_raw - 256) / 2.0) - 74.0;
  } else {
    rssi_dbm = ((float)rssi_raw / 2.0) - 74.0;
  }
  return rssi_dbm;
}

void cc1101_init(void) {
  spi_device_config_t devcfg = {
    .clock_speed_hz = 4 * 1000 * 1000, 
    .mode = 0,
    .cs_pin = GPIO_CS_PIN,
    .queue_size = 7
  };

  esp_err_t ret = spi_add_device(SPI3_HOST, SPI_DEVICE_CC1101, &devcfg);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Falha ao adicionar dispositivo SPI: %s", esp_err_to_name(ret));
    return;
  }

  cc1101_spi = spi_get_handle(SPI_DEVICE_CC1101);
  cc1101_strobe(CC1101_SRES);
  vTaskDelay(pdMS_TO_TICKS(50));

  cc1101_write_reg(CC1101_IOCFG0, 0x2E); 
  cc1101_write_reg(CC1101_IOCFG2, 0x2E); 

  uint8_t version = cc1101_read_reg(CC1101_VERSION | 0x40);
  if (version == 0x00 || version == 0xFF) {
    ESP_LOGE(TAG, "CC1101 não detectado!");
  } else {
    ESP_LOGI(TAG, "CC1101 Detectado! Versão: 0x%02X", version);
  }

  // Default Init similar to SmartRC
  cc1101_set_frequency(433920000);
}

void cc1101_enable_async_mode(uint32_t freq_hz) {
  if (cc1101_spi == NULL) return;

  ESP_LOGI(TAG, "Configurando CC1101 para modo Async (Sniffer)...");

  cc1101_strobe(CC1101_SIDLE);
  cc1101_strobe(CC1101_SRES); 
  vTaskDelay(pdMS_TO_TICKS(5));

  // SmartRC defaults for ASK/OOK Async
  cc1101_write_reg(CC1101_FSCTRL1,  0x06);
  cc1101_write_reg(CC1101_IOCFG2,   0x0D); // Serial Data Output
  cc1101_write_reg(CC1101_IOCFG0,   0x2E); // High Impedance (GDO2 is used for RMT)
  cc1101_write_reg(CC1101_PKTCTRL0, 0x32); // Async mode, Infinite packet length
  cc1101_write_reg(CC1101_PKTCTRL1, 0x04); // No addr check

  cc1101_set_frequency(freq_hz);

  // SmartRC "setCCMode(0)" defaults
  cc1101_set_rx_bandwidth(812.0); 
  cc1101_write_reg(CC1101_MDMCFG3, 0x93); 
  cc1101_set_modulation(2); // ASK/OOK (Sets MDMCFG2 and FREND0)

  cc1101_write_reg(CC1101_MDMCFG1,  0x02); // 2 preamble bytes, no FEC
  cc1101_write_reg(CC1101_MDMCFG0,  0xF8); // Channel spacing
  cc1101_set_deviation(47.0); 

  cc1101_write_reg(CC1101_MCSM0,    0x18); // FS Autocalibrate
  cc1101_write_reg(CC1101_FOCCFG,   0x16);
  cc1101_write_reg(CC1101_BSCFG,    0x1C);

  // AGC - Max Sensitivity
  cc1101_write_reg(CC1101_AGCCTRL2, 0xC7); 
  cc1101_write_reg(CC1101_AGCCTRL1, 0x00);
  cc1101_write_reg(CC1101_AGCCTRL0, 0xB2);

  cc1101_write_reg(CC1101_FREND1,   0x56);
  cc1101_set_pa(10); // Max power

  // Test Settings from SmartRC
  cc1101_write_reg(CC1101_FSCAL3,   0xE9);
  cc1101_write_reg(CC1101_FSCAL2,   0x2A);
  cc1101_write_reg(CC1101_FSCAL1,   0x00);
  cc1101_write_reg(CC1101_FSCAL0,   0x1F);
  cc1101_write_reg(CC1101_FSTEST,   0x59);
  cc1101_write_reg(CC1101_TEST2,    0x81);
  cc1101_write_reg(CC1101_TEST1,    0x35);
  cc1101_write_reg(CC1101_TEST0,    0x09);

  ESP_LOGI(TAG, "CC1101 configurado em Async Mode (GDO2 Active High)");
  cc1101_strobe(CC1101_SRX);
}

void cc1101_enable_fsk_mode(uint32_t freq_hz) {
  if (cc1101_spi == NULL) return;

  ESP_LOGI(TAG, "Configurando CC1101 para modo FSK (Sniffer)...");

  cc1101_strobe(CC1101_SIDLE);
  cc1101_strobe(CC1101_SRES);
  vTaskDelay(pdMS_TO_TICKS(5));

  cc1101_write_reg(CC1101_FSCTRL1,  0x06);
  cc1101_write_reg(CC1101_IOCFG2,   0x0D);
  cc1101_write_reg(CC1101_IOCFG0,   0x2E);
  cc1101_write_reg(CC1101_PKTCTRL0, 0x32);
  cc1101_write_reg(CC1101_PKTCTRL1, 0x04);
  cc1101_write_reg(CC1101_PKTLEN,   0x00);

  cc1101_set_frequency(freq_hz);
  cc1101_set_rx_bandwidth(812.0);
  cc1101_write_reg(CC1101_MDMCFG3, 0x93);

  cc1101_set_modulation(0); // 2-FSK

  cc1101_write_reg(CC1101_MDMCFG1,  0x02);
  cc1101_write_reg(CC1101_MDMCFG0,  0xF8);
  cc1101_set_deviation(47.0);

  cc1101_write_reg(CC1101_MCSM0,    0x18);
  cc1101_write_reg(CC1101_FOCCFG,   0x16);
  cc1101_write_reg(CC1101_BSCFG,    0x1C);

  cc1101_write_reg(CC1101_AGCCTRL2, 0xC7);
  cc1101_write_reg(CC1101_AGCCTRL1, 0x00);
  cc1101_write_reg(CC1101_AGCCTRL0, 0xB2);

  cc1101_write_reg(CC1101_FREND1,   0x56);
  cc1101_set_pa(10);

  cc1101_write_reg(CC1101_FSCAL3,   0xE9);
  cc1101_write_reg(CC1101_FSCAL2,   0x2A);
  cc1101_write_reg(CC1101_FSCAL1,   0x00);
  cc1101_write_reg(CC1101_FSCAL0,   0x1F);
  cc1101_write_reg(CC1101_FSTEST,   0x59);
  cc1101_write_reg(CC1101_TEST2,    0x81);
  cc1101_write_reg(CC1101_TEST1,    0x35);
  cc1101_write_reg(CC1101_TEST0,    0x09);

  ESP_LOGI(TAG, "CC1101 configurado em FSK Mode");
  cc1101_strobe(CC1101_SRX);
}

void cc1101_set_preset(cc1101_preset_t preset, uint32_t freq_hz) {
  s_active_preset_id = (uint8_t)preset;

  switch (preset) {
    case CC1101_PRESET_IDLE:
      cc1101_strobe(CC1101_SIDLE);
      break;

    case CC1101_PRESET_OOK_270KHZ:
      cc1101_enable_async_mode(freq_hz);
      cc1101_set_rx_bandwidth(270.0);
      break;

    case CC1101_PRESET_OOK_650KHZ:
      cc1101_enable_async_mode(freq_hz);
      cc1101_set_rx_bandwidth(650.0);
      break;

    case CC1101_PRESET_2FSK_2KHZ:
      cc1101_enable_fsk_mode(freq_hz);
      cc1101_set_rx_bandwidth(58.0);
      cc1101_set_deviation(2.38);
      break;

    case CC1101_PRESET_2FSK_47KHZ:
      cc1101_enable_fsk_mode(freq_hz);
      cc1101_set_rx_bandwidth(270.0);
      cc1101_set_deviation(47.6);
      break;

    case CC1101_PRESET_2FSK_95KHZ:
      cc1101_enable_fsk_mode(freq_hz);
      cc1101_set_rx_bandwidth(540.0);
      cc1101_set_deviation(95.2);
      break;

    case CC1101_PRESET_OOK_800KHZ:
    default:
      cc1101_enable_async_mode(freq_hz);
      cc1101_set_rx_bandwidth(812.0); 
      break;
  }
}

uint8_t cc1101_get_active_preset_id(void) {
  return s_active_preset_id;
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
