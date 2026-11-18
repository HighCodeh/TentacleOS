// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#include "cc1101_bench.h"
#include <string.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cc1101.h"
#include "cc1101_lease.h"
#include "pin_def.h"
#define CONFIG_SIZE               47U
#define PA_SIZE                   8U
#define STATUS_READ               0x40U
#define STATE_MASK                0x1FU
#define STATE_IDLE                1U
#define STATE_RX                  13U
#define STATE_TX                  19U
#define STATE_TIMEOUT_US          100000LL
#define FIFO_ERROR                0x80U
#define CRC_OK                    0x80U
#define STATUS_STABILITY_ATTEMPTS 4U
#define TX_FIFO_COUNT_MASK        0x7FU
#define PACKET_STATUS_SIZE        2U
#define XTAL_HZ                   26000000ULL
#define FREQ_SCALE                65536ULL
#define RSSI_OFFSET_HALF          148
static const char *TAG = "CC1101_BENCH";
static uint8_t s_saved_config[CONFIG_SIZE];
static uint8_t s_saved_pa[PA_SIZE];
static uint8_t s_saved_path;
static bool s_has_snapshot;
static bool s_is_cw;
static uint8_t s_pa;
/* TI packet engine: 26 MHz, 9.596 kbit/s 2-FSK, 20.63 kHz deviation,
   101.56 kHz RX BW, 4-byte preamble, D391 sync, fixed 32 bytes + CRC. */
static const uint8_t SETTINGS[][2] = {
    {CC1101_IOCFG0, 0x2E},   {CC1101_IOCFG2, 0x2E},   {CC1101_FIFOTHR, 0x07},
    {CC1101_SYNC1, 0xD3},    {CC1101_SYNC0, 0x91},    {CC1101_PKTLEN, CC1101_BENCH_PAYLOAD_SIZE},
    {CC1101_PKTCTRL1, 0x04}, {CC1101_PKTCTRL0, 0x04}, {CC1101_ADDR, 0x00},
    {CC1101_CHANNR, 0x00},   {CC1101_FSCTRL1, 0x06},  {CC1101_FSCTRL0, 0x00},
    {CC1101_MDMCFG4, 0xC8},  {CC1101_MDMCFG3, 0x83},  {CC1101_MDMCFG2, 0x02},
    {CC1101_MDMCFG1, 0x20},  {CC1101_MDMCFG0, 0xF8},  {CC1101_DEVIATN, 0x35},
    {CC1101_MCSM2, 0x07},    {CC1101_MCSM1, 0x00},    {CC1101_MCSM0, 0x18},
    {CC1101_FOCCFG, 0x16},   {CC1101_BSCFG, 0x6C},    {CC1101_AGCCTRL2, 0xC7},
    {CC1101_AGCCTRL1, 0x00}, {CC1101_AGCCTRL0, 0x91}, {CC1101_FREND1, 0x56},
    {CC1101_FREND0, 0x10},   {CC1101_FSCAL3, 0xE9},   {CC1101_FSCAL2, 0x2A},
    {CC1101_FSCAL1, 0x00},   {CC1101_FSCAL0, 0x1F},   {CC1101_TEST2, 0x81},
    {CC1101_TEST1, 0x35},    {CC1101_TEST0, 0x09},
};
static esp_err_t wait_state(uint8_t expected);
static esp_err_t idle(void);
static esp_err_t read_status_stable(uint8_t reg, uint8_t *out_value);
static esp_err_t wait_packet_complete(void);
static esp_err_t power_code(uint32_t hz, int8_t dbm, uint8_t *out_code);

esp_err_t cc1101_bench_prepare(const cc1101_bench_config_t *config) {
  if (config == NULL || config->path < 1 || config->path > 4 || s_has_snapshot)
    return ESP_ERR_INVALID_ARG;
  uint8_t pa = 0;
  esp_err_t error = power_code(config->frequency_hz, config->power_dbm, &pa);
  if (error != ESP_OK)
    return error;
  (void)cc1101_lease_take_error();
  cc1101_init();
  if (!cc1101_is_initialized())
    return ESP_ERR_INVALID_STATE;
  error = cc1101_lease_take_error();
  if (error != ESP_OK)
    return error;
  error = idle();
  if (error != ESP_OK)
    return error;
  uint8_t version = cc1101_read_reg(CC1101_VERSION | STATUS_READ);
  if (version == 0 || version == UINT8_MAX)
    return ESP_ERR_NOT_FOUND;
  for (uint8_t i = 0; i < CONFIG_SIZE; ++i)
    s_saved_config[i] = cc1101_read_reg(i);
  cc1101_read_burst(CC1101_PATABLE, s_saved_pa, PA_SIZE);
  error = cc1101_lease_take_error();
  if (error != ESP_OK)
    return error;
  error = gpio_input_enable(GPIO_RF_SW_V1_PIN);
  if (error == ESP_OK)
    error = gpio_input_enable(GPIO_RF_SW_V2_PIN);
  if (error != ESP_OK)
    return error;
  s_saved_path = (gpio_get_level(GPIO_RF_SW_V1_PIN) << 1) | gpio_get_level(GPIO_RF_SW_V2_PIN);
  s_has_snapshot = true;
  s_is_cw = config->is_cw;
  s_pa = pa;
  for (size_t i = 0; i < sizeof(SETTINGS) / sizeof(SETTINGS[0]); ++i)
    cc1101_write_reg(SETTINGS[i][0], SETTINGS[i][1]);
  uint32_t word = (uint32_t)(((uint64_t)config->frequency_hz * FREQ_SCALE) / XTAL_HZ);
  cc1101_write_reg(CC1101_FREQ2, word >> 16);
  cc1101_write_reg(CC1101_FREQ1, word >> 8);
  cc1101_write_reg(CC1101_FREQ0, word);
  uint8_t pa_table[PA_SIZE] = {0};
  pa_table[config->is_cw ? 1 : 0] = pa;
  cc1101_write_burst(CC1101_PATABLE, pa_table, PA_SIZE);
  if (config->is_cw) {
    cc1101_write_reg(CC1101_PKTCTRL0, 0x32);
    cc1101_write_reg(CC1101_MDMCFG2, 0x30);
    cc1101_write_reg(CC1101_FREND0, 0x11);
  }
  error = gpio_set_level(GPIO_RF_SW_V1_PIN, (config->path - 1) >> 1);
  if (error == ESP_OK)
    error = gpio_set_level(GPIO_RF_SW_V2_PIN, (config->path - 1) & 1);
  if (error != ESP_OK)
    return error;
  vTaskDelay(1);
  if (gpio_get_level(GPIO_RF_SW_V1_PIN) != ((config->path - 1) >> 1) ||
      gpio_get_level(GPIO_RF_SW_V2_PIN) != ((config->path - 1) & 1))
    return ESP_ERR_INVALID_RESPONSE;
  uint32_t actual_word = ((uint32_t)cc1101_read_reg(CC1101_FREQ2) << 16) |
                         ((uint32_t)cc1101_read_reg(CC1101_FREQ1) << 8) |
                         cc1101_read_reg(CC1101_FREQ0);
  if (actual_word != word)
    return ESP_ERR_INVALID_RESPONSE;
  for (size_t i = 0; i < sizeof(SETTINGS) / sizeof(SETTINGS[0]); ++i) {
    uint8_t reg = SETTINGS[i][0];
    if (config->is_cw && (reg == CC1101_PKTCTRL0 || reg == CC1101_MDMCFG2 || reg == CC1101_FREND0))
      continue;
    if (cc1101_read_reg(reg) != SETTINGS[i][1])
      return ESP_ERR_INVALID_RESPONSE;
  }
  uint8_t read_pa[PA_SIZE] = {0};
  cc1101_read_burst(CC1101_PATABLE, read_pa, PA_SIZE);
  if (memcmp(pa_table, read_pa, PA_SIZE) != 0)
    return ESP_ERR_INVALID_RESPONSE;
  error = cc1101_lease_take_error();
  if (error != ESP_OK)
    return error;
  cc1101_strobe(CC1101_SCAL);
  vTaskDelay(pdMS_TO_TICKS(5));
  error = wait_state(STATE_IDLE);
  ESP_LOGI(TAG,
           "config hz=%lu word=%06lx pa=%02x path=%u cw=%u result=%s",
           (unsigned long)config->frequency_hz,
           (unsigned long)word,
           pa,
           config->path,
           config->is_cw,
           esp_err_to_name(error));
  return error;
}

esp_err_t cc1101_bench_enter(bool is_tx) {
  if (!s_has_snapshot || (is_tx && !s_is_cw))
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = idle();
  if (error != ESP_OK)
    return error;
  if (is_tx) {
    error = gpio_set_direction(GPIO_CC1101_GDO0_PIN, GPIO_MODE_OUTPUT);
    if (error == ESP_OK)
      error = gpio_set_level(GPIO_CC1101_GDO0_PIN, 1);
    if (error != ESP_OK)
      return error;
  } else {
    cc1101_strobe(CC1101_SFRX);
  }
  cc1101_strobe(is_tx ? CC1101_STX : CC1101_SRX);
  return wait_state(is_tx ? STATE_TX : STATE_RX);
}

esp_err_t cc1101_bench_packet(const uint8_t *payload) {
  if (payload == NULL || !s_has_snapshot || s_is_cw)
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = idle();
  if (error != ESP_OK)
    return error;
  cc1101_strobe(CC1101_SFTX);
  cc1101_write_burst(CC1101_TXFIFO, payload, CC1101_BENCH_PAYLOAD_SIZE);
  error = cc1101_lease_take_error();
  if (error != ESP_OK)
    return error;
  uint8_t count = 0;
  error = read_status_stable(CC1101_TXBYTES, &count);
  if (error != ESP_OK)
    return error;
  if (count != CC1101_BENCH_PAYLOAD_SIZE)
    return ESP_ERR_INVALID_RESPONSE;
  cc1101_strobe(CC1101_STX);
  error = wait_packet_complete();
  esp_err_t spi_error = cc1101_lease_take_error();
  return error == ESP_OK ? spi_error : error;
}

esp_err_t cc1101_bench_sample(cc1101_bench_sample_t *out_sample) {
  if (out_sample == NULL || !s_has_snapshot)
    return ESP_ERR_INVALID_ARG;
  memset(out_sample, 0, sizeof(*out_sample));
  out_sample->state = cc1101_read_reg(CC1101_MARCSTATE | STATUS_READ) & STATE_MASK;
  out_sample->pa = s_pa;
  out_sample->rssi_half_dbm = (int8_t)cc1101_read_reg(CC1101_RSSI | STATUS_READ) - RSSI_OFFSET_HALF;
  out_sample->frequency_error = (int8_t)cc1101_read_reg(CC1101_FREQEST | STATUS_READ);
  uint8_t count = cc1101_read_reg(CC1101_RXBYTES | STATUS_READ);
  esp_err_t error = cc1101_lease_take_error();
  if (error != ESP_OK)
    return error;
  if ((count & FIFO_ERROR) != 0)
    return ESP_ERR_INVALID_SIZE;
  if (out_sample->state == STATE_IDLE && count >= CC1101_BENCH_PAYLOAD_SIZE + PACKET_STATUS_SIZE) {
    uint8_t bytes[CC1101_BENCH_PAYLOAD_SIZE + PACKET_STATUS_SIZE] = {0};
    cc1101_read_burst(CC1101_RXFIFO, bytes, sizeof(bytes));
    memcpy(out_sample->payload, bytes, CC1101_BENCH_PAYLOAD_SIZE);
    out_sample->has_packet = true;
    out_sample->has_crc_error = (bytes[sizeof(bytes) - 1] & CRC_OK) == 0;
    out_sample->rssi_half_dbm = (int8_t)bytes[CC1101_BENCH_PAYLOAD_SIZE] - RSSI_OFFSET_HALF;
  }
  return cc1101_lease_take_error();
}

esp_err_t cc1101_bench_restore(void) {
  (void)cc1101_lease_take_error();
  esp_err_t error = idle();
  if (error != ESP_OK)
    return error;
  if (!s_has_snapshot)
    return ESP_OK;
  error = gpio_set_direction(GPIO_CC1101_GDO0_PIN, GPIO_MODE_INPUT);
  if (error != ESP_OK)
    return error;
  cc1101_strobe(CC1101_SFRX);
  cc1101_strobe(CC1101_SFTX);
  /* Restore only registers changed by this backend; FSTEST/AGCTEST are not configuration. */
  for (size_t i = 0; i < sizeof(SETTINGS) / sizeof(SETTINGS[0]); ++i) {
    uint8_t reg = SETTINGS[i][0];
    cc1101_write_reg(reg, s_saved_config[reg]);
  }
  for (uint8_t reg = CC1101_FREQ2; reg <= CC1101_FREQ0; ++reg)
    cc1101_write_reg(reg, s_saved_config[reg]);
  cc1101_write_burst(CC1101_PATABLE, s_saved_pa, PA_SIZE);
  error = gpio_set_level(GPIO_RF_SW_V1_PIN, s_saved_path >> 1);
  if (error == ESP_OK)
    error = gpio_set_level(GPIO_RF_SW_V2_PIN, s_saved_path & 1);
  if (error != ESP_OK)
    return error;
  for (size_t i = 0; i < sizeof(SETTINGS) / sizeof(SETTINGS[0]); ++i) {
    uint8_t reg = SETTINGS[i][0];
    uint8_t actual = cc1101_read_reg(reg);
    if (actual != s_saved_config[reg]) {
      ESP_LOGE(TAG, "restore reg=%02x expected=%02x actual=%02x", reg, s_saved_config[reg], actual);
      return ESP_ERR_INVALID_RESPONSE;
    }
  }
  for (uint8_t reg = CC1101_FREQ2; reg <= CC1101_FREQ0; ++reg)
    if (cc1101_read_reg(reg) != s_saved_config[reg])
      return ESP_ERR_INVALID_RESPONSE;
  uint8_t pa[PA_SIZE] = {0};
  cc1101_read_burst(CC1101_PATABLE, pa, PA_SIZE);
  if (memcmp(pa, s_saved_pa, PA_SIZE) != 0)
    return ESP_ERR_INVALID_RESPONSE;
  error = cc1101_lease_take_error();
  if (error == ESP_OK)
    s_has_snapshot = false;
  return error;
}

/* TI SWRS061I sections 10.3/26: status reads may overlap hardware updates.
   The scheduler can miss the entire TX state of a short packet. Confirm a
   drained FIFO AND return to IDLE, rather than requiring an observed TX edge. */
static esp_err_t read_status_stable(uint8_t reg, uint8_t *out_value) {
  uint8_t previous = cc1101_read_reg(reg | STATUS_READ);
  for (unsigned i = 0; i < STATUS_STABILITY_ATTEMPTS; i++) {
    uint8_t value = cc1101_read_reg(reg | STATUS_READ);
    esp_err_t error = cc1101_lease_take_error();
    if (error != ESP_OK)
      return error;
    if (value == previous) {
      *out_value = value;
      return ESP_OK;
    }
    previous = value;
  }
  return ESP_ERR_INVALID_RESPONSE;
}
static esp_err_t wait_packet_complete(void) {
  int64_t end = esp_timer_get_time() + STATE_TIMEOUT_US;
  do {
    uint8_t state = 0, count = 0;
    esp_err_t error = read_status_stable(CC1101_MARCSTATE, &state);
    if (error == ESP_OK)
      error = read_status_stable(CC1101_TXBYTES, &count);
    if (error != ESP_OK)
      return error;
    if (count & FIFO_ERROR)
      return ESP_ERR_INVALID_RESPONSE;
    if ((state & STATE_MASK) == STATE_IDLE && (count & TX_FIFO_COUNT_MASK) == 0)
      return ESP_OK;
    vTaskDelay(1);
  } while (esp_timer_get_time() < end);
  return ESP_ERR_TIMEOUT;
}
static esp_err_t wait_state(uint8_t expected) {
  int64_t end = esp_timer_get_time() + STATE_TIMEOUT_US;
  do {
    uint8_t state = cc1101_read_reg(CC1101_MARCSTATE | STATUS_READ) & STATE_MASK;
    esp_err_t error = cc1101_lease_take_error();
    if (error != ESP_OK)
      return error;
    if (state == expected)
      return ESP_OK;
    vTaskDelay(1);
  } while (esp_timer_get_time() < end);
  return ESP_ERR_TIMEOUT;
}
static esp_err_t idle(void) {
  cc1101_strobe(CC1101_SIDLE);
  return wait_state(STATE_IDLE);
}
static esp_err_t power_code(uint32_t hz, int8_t dbm, uint8_t *out_code) {
  static const int8_t LEVELS[] = {-30, -20, -10, 0, 5, 10};
  static const uint8_t CODES[][6] = {
      {0x12, 0x0D, 0x34, 0x51, 0x85, 0xC2},
      {0x12, 0x0E, 0x34, 0x60, 0x84, 0xC0},
      {0x03, 0x17, 0x26, 0x50, 0x86, 0xC5},
      {0x03, 0x0E, 0x27, 0x8E, 0x84, 0xC3},
  };
  uint8_t band;
  if (hz >= 300000000U && hz <= 348000000U)
    band = 0;
  else if (hz >= 387000000U && hz <= 464000000U)
    band = 1;
  else if (hz >= 779000000U && hz <= 899999999U)
    band = 2;
  else if (hz >= 900000000U && hz <= 928000000U)
    band = 3;
  else
    return ESP_ERR_INVALID_ARG;
  for (size_t i = 0; i < sizeof(LEVELS) / sizeof(LEVELS[0]); ++i) {
    if (dbm == LEVELS[i]) {
      *out_code = CODES[band][i];
      return ESP_OK;
    }
  }
  return ESP_ERR_INVALID_ARG;
}
