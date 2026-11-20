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

#include "cc1101.h"
#include "cc1101_config.h"
#include "cc1101_rf_path.h"
#include "cc1101_lease.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <rom/ets_sys.h>

#include "pin_def.h"
#include "spi.h"

#define CC1101_SPI_CLOCK_HZ             (1 * 1000 * 1000)
#define CC1101_SPI_QUEUE_SIZE           7
#define CC1101_STATE_TIMEOUT_US         100000
#define CC1101_RF_SWITCH_SETTLE_US      5
#define CC1101_FREQ_SCALE               65536UL
#define CC1101_STATUS_READ              0x40U
#define CC1101_FSCAL2_HIGH_VCO          0x20U
#define CC1101_TEST0_AUTO_VCO           0x0BU
#define CC1101_TEST0_HIGH_VCO           0x09U
#define CC1101_VCO_315_THRESHOLD_HZ     322880000UL
#define CC1101_VCO_433_THRESHOLD_HZ     430500000UL
#define CC1101_VCO_HIGH_THRESHOLD_HZ    861000000UL
#define CC1101_MARCSTATE_IDLE           0x01U
#define CC1101_MARCSTATE_RX             0x0DU
#define CC1101_MODEM_MANTISSA_MAX       255U
#define CC1101_RATE_EXPONENT_MAX        15U
#define CC1101_RATE_SCALE               268435456.0f
#define CC1101_RATE_MANTISSA_BASE       256U
#define CC1101_CHSP_EXPONENT_MAX        3U
#define CC1101_CHSP_SCALE               262144.0f
#define CC1101_DEVIATION_EXPONENT_MAX   7U
#define CC1101_DEVIATION_MANTISSA_MAX   7U
#define CC1101_DEVIATION_MANTISSA_BASE  8U
#define CC1101_DEVIATION_SCALE          131072.0f
#define CC1101_KHZ_TO_HZ                1000.0f
#define CC1101_BANDWIDTH_EXPONENT_MAX   3U
#define CC1101_BANDWIDTH_MANTISSA_MAX   3U
#define CC1101_BANDWIDTH_MANTISSA_BASE  4U
#define CC1101_BANDWIDTH_DIVISOR        8U
#define CC1101_BANDWIDTH_EXPONENT_SHIFT 6U
#define CC1101_BANDWIDTH_MANTISSA_SHIFT 4U
#define CC1101_DEVIATION_EXPONENT_SHIFT 4U
#define CC1101_RATE_EXPONENT_MASK       0x0FU
#define CC1101_BANDWIDTH_MASK           0xF0U
#define CC1101_CHSP_EXPONENT_MASK       0x03U
#define CC1101_DEFAULT_PA_DBM           10
#define CC1101_GDO_HIGH_IMPEDANCE       0x2EU
#define CC1101_GDO_ASYNC_DATA           0x0DU
#define CC1101_PKT_ASYNC                0x32U
#define CC1101_MAX_TX_FIFO              61
#define CC1101_PA_TABLE_SIZE            8
#define CC1101_RSSI_OFFSET              74
#define CC1101_DEFAULT_FREQ_HZ          433920000
#define CC1101_XTAL_FREQ_HZ             26000000

#define CC1101_MARCSTATE_TX     0x13
#define CC1101_MARCSTATE_TX_END 0x14
#define CC1101_MARCSTATE_RXTX   0x15
#define CC1101_MARCSTATE_MASK   0x1F

#define CC1101_MOD_2FSK 0
#define CC1101_MOD_GFSK 1
#define CC1101_MOD_ASK  2
#define CC1101_MOD_4FSK 3
#define CC1101_MOD_MSK  4

#define CC1101_BUS_LOCK_TIMEOUT_MS    1000
#define CC1101_PACKET_TX_TIMEOUT_US   2000000
#define CC1101_PACKET_FORMAT_MASK     0x30U
#define CC1101_PACKET_LENGTH_MASK     0x03U
#define CC1101_PACKET_VARIABLE_LENGTH 0x01U
#define CC1101_TX_FIFO_UNDERFLOW      0x80U
#define CC1101_TX_FIFO_COUNT_MASK     0x7FU
#define CC1101_MARCSTATE_FSTXON       0x12U

#define CC1101_RETURN_ON_ERROR(operation)    \
  do {                                       \
    esp_err_t operation_error = (operation); \
    if (operation_error != ESP_OK)           \
      return operation_error;                \
  } while (0)

#define CC1101_RSSI_SIGN_THRESHOLD 128
#define CC1101_RSSI_SIGN_RANGE     256
#define CC1101_RSSI_DB_PER_STEP    0.5f

static const char *TAG = "CC1101_DRIVER";
static spi_device_handle_t s_cc1101_spi = NULL;

static uint32_t s_current_freq_hz = CC1101_DEFAULT_FREQ_HZ;
static bool s_is_tuned;
static bool s_is_initialized;
static uint8_t s_current_modulation = CC1101_MOD_ASK;
static int s_current_pa_dbm = CC1101_DEFAULT_PA_DBM;
static uint8_t s_active_preset_id = 0;

static const uint8_t PA_TABLE_315[CC1101_PA_TABLE_SIZE] = {
    0x12, 0x0D, 0x1C, 0x34, 0x51, 0x85, 0xCB, 0xC2}; // 300 - 348 MHz
static const uint8_t PA_TABLE_433[CC1101_PA_TABLE_SIZE] = {
    0x12, 0x0E, 0x1D, 0x34, 0x60, 0x84, 0xC8, 0xC0}; // 387 - 464 MHz
static const uint8_t PA_TABLE_868[CC1101_PA_TABLE_SIZE] = {
    0x03, 0x17, 0x1D, 0x26, 0x37, 0x50, 0x86, 0xCD}; // 779 - 899 MHz
static const uint8_t PA_TABLE_915[CC1101_PA_TABLE_SIZE] = {
    0x03, 0x0E, 0x1E, 0x27, 0x38, 0x8E, 0x84, 0xCC}; // 900 - 928 MHz

static esp_err_t write_burst_impl(uint8_t reg, const uint8_t *buf, uint8_t len);
static esp_err_t write_reg_impl(uint8_t reg, uint8_t val);
static uint8_t read_reg_impl(uint8_t reg);
static esp_err_t get_register_impl(uint8_t reg, uint8_t *out_value);
static void read_burst_impl(uint8_t reg, uint8_t *buf, uint8_t len);
static esp_err_t strobe_impl(uint8_t cmd);
static esp_err_t calibrate_impl(void);
static esp_err_t set_frequency_impl(uint32_t freq_hz);
static esp_err_t set_rx_bandwidth_impl(float khz);
static esp_err_t set_data_rate_impl(float baud);
static esp_err_t set_deviation_impl(float dev);
static esp_err_t set_modulation_impl(uint8_t modulation);
static esp_err_t set_pa_impl(int dbm);
static void set_channel_impl(uint8_t channel);
static esp_err_t set_chsp_impl(float khz);
static void set_sync_mode_impl(uint8_t mode);
static void set_fec_impl(bool enable);
static void set_preamble_impl(uint8_t preamble_bytes);
static void set_dc_filter_off_impl(bool disable);
static void set_manchester_impl(bool enable);
static esp_err_t send_data_impl(const uint8_t *data, size_t len);
static esp_err_t init_impl(void);
static bool is_initialized_impl(void);
static esp_err_t recover_impl(void);
static esp_err_t enable_async_mode_impl(uint32_t freq_hz);
static esp_err_t enable_fsk_mode_impl(uint32_t freq_hz);
static esp_err_t set_preset_impl(cc1101_preset_t preset, uint32_t freq_hz);
static esp_err_t set_radio_config_impl(const cc1101_radio_config_t *config, uint32_t freq_hz);
static esp_err_t get_radio_config_impl(cc1101_radio_config_t *out_config);
static uint8_t get_active_preset_id_impl(void);
static esp_err_t enter_tx_mode_impl(void);
static esp_err_t enter_rx_mode_impl(void);

esp_err_t cc1101_write_burst(uint8_t reg, const uint8_t *buf, uint8_t len) {
  if (!cc1101_lease_enter())
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = write_burst_impl(reg, buf, len);
  cc1101_lease_record_error(error);
  cc1101_lease_leave();
  return error;
}

esp_err_t cc1101_write_reg(uint8_t reg, uint8_t val) {
  if (!cc1101_lease_enter())
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = write_reg_impl(reg, val);
  cc1101_lease_record_error(error);
  cc1101_lease_leave();
  return error;
}

uint8_t cc1101_read_reg(uint8_t reg) {
  if (!cc1101_lease_enter())
    return 0;
  uint8_t result = read_reg_impl(reg);
  cc1101_lease_leave();
  return result;
}

esp_err_t cc1101_get_register(uint8_t reg, uint8_t *out_value) {
  if (!cc1101_lease_enter())
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = get_register_impl(reg, out_value);
  cc1101_lease_record_error(error);
  cc1101_lease_leave();
  return error;
}

void cc1101_read_burst(uint8_t reg, uint8_t *buf, uint8_t len) {
  if (!cc1101_lease_enter()) {
    if (buf != NULL)
      memset(buf, 0, len);
    return;
  }
  read_burst_impl(reg, buf, len);
  cc1101_lease_leave();
}

esp_err_t cc1101_strobe(uint8_t cmd) {
  if (!cc1101_lease_enter())
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = strobe_impl(cmd);
  cc1101_lease_record_error(error);
  cc1101_lease_leave();
  return error;
}

esp_err_t cc1101_calibrate(void) {
  if (!cc1101_lease_enter())
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = calibrate_impl();
  cc1101_lease_record_error(error);
  cc1101_lease_leave();
  return error;
}

esp_err_t cc1101_set_frequency(uint32_t freq_hz) {
  if (!cc1101_lease_enter())
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = set_frequency_impl(freq_hz);
  cc1101_lease_record_error(error);
  cc1101_lease_leave();
  return error;
}

esp_err_t cc1101_set_rx_bandwidth(float khz) {
  if (!cc1101_lease_enter())
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = set_rx_bandwidth_impl(khz);
  cc1101_lease_record_error(error);
  cc1101_lease_leave();
  return error;
}

esp_err_t cc1101_set_data_rate(float baud) {
  if (!cc1101_lease_enter())
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = set_data_rate_impl(baud);
  cc1101_lease_record_error(error);
  cc1101_lease_leave();
  return error;
}

esp_err_t cc1101_set_deviation(float dev) {
  if (!cc1101_lease_enter())
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = set_deviation_impl(dev);
  cc1101_lease_record_error(error);
  cc1101_lease_leave();
  return error;
}

esp_err_t cc1101_set_modulation(uint8_t modulation) {
  if (!cc1101_lease_enter())
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = set_modulation_impl(modulation);
  cc1101_lease_record_error(error);
  cc1101_lease_leave();
  return error;
}

esp_err_t cc1101_set_pa(int dbm) {
  if (!cc1101_lease_enter())
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = set_pa_impl(dbm);
  cc1101_lease_record_error(error);
  cc1101_lease_leave();
  return error;
}

void cc1101_set_channel(uint8_t channel) {
  if (!cc1101_lease_enter())
    return;
  set_channel_impl(channel);
  cc1101_lease_leave();
}

esp_err_t cc1101_set_chsp(float khz) {
  if (!cc1101_lease_enter())
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = set_chsp_impl(khz);
  cc1101_lease_record_error(error);
  cc1101_lease_leave();
  return error;
}

void cc1101_set_sync_mode(uint8_t mode) {
  if (!cc1101_lease_enter())
    return;
  set_sync_mode_impl(mode);
  cc1101_lease_leave();
}

void cc1101_set_fec(bool enable) {
  if (!cc1101_lease_enter())
    return;
  set_fec_impl(enable);
  cc1101_lease_leave();
}

void cc1101_set_preamble(uint8_t preamble_bytes) {
  if (!cc1101_lease_enter())
    return;
  set_preamble_impl(preamble_bytes);
  cc1101_lease_leave();
}

void cc1101_set_dc_filter_off(bool disable) {
  if (!cc1101_lease_enter())
    return;
  set_dc_filter_off_impl(disable);
  cc1101_lease_leave();
}

void cc1101_set_manchester(bool enable) {
  if (!cc1101_lease_enter())
    return;
  set_manchester_impl(enable);
  cc1101_lease_leave();
}

esp_err_t cc1101_send_data(const uint8_t *data, size_t len) {
  if (!cc1101_lease_enter())
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = send_data_impl(data, len);
  cc1101_lease_record_error(error);
  cc1101_lease_leave();
  return error;
}

esp_err_t cc1101_init(void) {
  if (!cc1101_lease_enter())
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = init_impl();
  cc1101_lease_record_error(error);
  cc1101_lease_leave();
  return error;
}

bool cc1101_is_initialized(void) {
  if (!cc1101_lease_enter())
    return false;
  bool result = is_initialized_impl();
  cc1101_lease_leave();
  return result;
}

esp_err_t cc1101_recover(void) {
  if (!cc1101_lease_enter())
    return ESP_ERR_INVALID_STATE;
  esp_err_t result = recover_impl();
  cc1101_lease_leave();
  return result;
}

esp_err_t cc1101_enable_async_mode(uint32_t freq_hz) {
  if (!cc1101_lease_enter())
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = enable_async_mode_impl(freq_hz);
  cc1101_lease_record_error(error);
  cc1101_lease_leave();
  return error;
}

esp_err_t cc1101_enable_fsk_mode(uint32_t freq_hz) {
  if (!cc1101_lease_enter())
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = enable_fsk_mode_impl(freq_hz);
  cc1101_lease_record_error(error);
  cc1101_lease_leave();
  return error;
}

esp_err_t cc1101_set_preset(cc1101_preset_t preset, uint32_t freq_hz) {
  if (!cc1101_lease_enter())
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = set_preset_impl(preset, freq_hz);
  cc1101_lease_record_error(error);
  cc1101_lease_leave();
  return error;
}

esp_err_t cc1101_set_radio_config(const cc1101_radio_config_t *config, uint32_t freq_hz) {
  if (!cc1101_lease_enter())
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = set_radio_config_impl(config, freq_hz);
  cc1101_lease_record_error(error);
  cc1101_lease_leave();
  return error;
}

esp_err_t cc1101_get_radio_config(cc1101_radio_config_t *out_config) {
  if (!cc1101_lease_enter())
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = get_radio_config_impl(out_config);
  cc1101_lease_record_error(error);
  cc1101_lease_leave();
  return error;
}

uint8_t cc1101_get_active_preset_id(void) {
  if (!cc1101_lease_enter())
    return 0;
  uint8_t result = get_active_preset_id_impl();
  cc1101_lease_leave();
  return result;
}

esp_err_t cc1101_enter_tx_mode(void) {
  if (!cc1101_lease_enter())
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = enter_tx_mode_impl();
  cc1101_lease_record_error(error);
  cc1101_lease_leave();
  return error;
}

esp_err_t cc1101_enter_rx_mode(void) {
  if (!cc1101_lease_enter())
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = enter_rx_mode_impl();
  cc1101_lease_record_error(error);
  cc1101_lease_leave();
  return error;
}

static esp_err_t read_register(uint8_t reg, uint8_t *out_value);

static esp_err_t wait_for_state(uint8_t state) {
  int64_t deadline = esp_timer_get_time() + CC1101_STATE_TIMEOUT_US;
  do {
    uint8_t current;
    CC1101_RETURN_ON_ERROR(read_register(CC1101_MARCSTATE | CC1101_STATUS_READ, &current));
    if ((current & CC1101_MARCSTATE_MASK) == state)
      return ESP_OK;
    vTaskDelay(1);
  } while (esp_timer_get_time() < deadline);
  ESP_LOGE(TAG, "Radio state 0x%02X timeout", state);
  return ESP_ERR_TIMEOUT;
}

static esp_err_t enter_idle(void) {
  CC1101_RETURN_ON_ERROR(strobe_impl(CC1101_SIDLE));
  return wait_for_state(CC1101_MARCSTATE_IDLE);
}

static esp_err_t configure_sniffer(uint32_t freq_hz, const cc1101_async_profile_t *profile) {
  if (cc1101_rf_path_for_frequency(freq_hz) == CC1101_RF_PATH_NONE)
    return ESP_ERR_INVALID_ARG;
  s_is_tuned = false;
  s_active_preset_id = CC1101_PRESET_IDLE;
  CC1101_RETURN_ON_ERROR(enter_idle());
  CC1101_RETURN_ON_ERROR(strobe_impl(CC1101_SRES));
  CC1101_RETURN_ON_ERROR(wait_for_state(CC1101_MARCSTATE_IDLE));

  static const uint8_t ASYNC_REGISTERS[][2] = {
      {CC1101_FSCTRL1, 0x06},
      {CC1101_IOCFG0, CC1101_GDO_ASYNC_DATA},
      {CC1101_IOCFG2, CC1101_GDO_HIGH_IMPEDANCE},
      {CC1101_PKTCTRL0, CC1101_PKT_ASYNC},
      {CC1101_PKTLEN, 0x00},
      {CC1101_CHANNR, 0x00},
      {CC1101_MCSM0, 0x18},
      {CC1101_BSCFG, 0x1C},
      {CC1101_FSCAL3, 0xE9},
      {CC1101_FSCAL2, 0x2A},
      {CC1101_FSCAL1, 0x00},
      {CC1101_FSCAL0, 0x1F},
      {CC1101_FSTEST, 0x59},
      {CC1101_TEST2, 0x81},
      {CC1101_TEST1, 0x35},
  };
  for (size_t i = 0; i < sizeof(ASYNC_REGISTERS) / sizeof(ASYNC_REGISTERS[0]); ++i)
    CC1101_RETURN_ON_ERROR(write_reg_impl(ASYNC_REGISTERS[i][0], ASYNC_REGISTERS[i][1]));
  for (size_t i = 0; i < CC1101_PROFILE_REGISTER_COUNT; ++i)
    CC1101_RETURN_ON_ERROR(write_reg_impl(CC1101_PROFILE_REGISTERS[i], profile->values[i]));
  CC1101_RETURN_ON_ERROR(set_modulation_impl(profile->modulation));
  CC1101_RETURN_ON_ERROR(set_frequency_impl(freq_hz));
  esp_err_t error = enter_rx_mode_impl();
  if (error != ESP_OK)
    s_is_tuned = false;
  return error;
}

static esp_err_t cc1101_reset_radio(void) {
  if (s_cc1101_spi == NULL) {
    cc1101_lease_record_error(ESP_ERR_INVALID_STATE);
    return ESP_ERR_INVALID_STATE;
  }

  if (!spi_bus_lock_take(CC1101_BUS_LOCK_TIMEOUT_MS)) {
    ESP_LOGE(TAG, "SPI3 bus lock timeout");
    cc1101_lease_record_error(ESP_ERR_TIMEOUT);
    return ESP_ERR_TIMEOUT;
  }

  spi_transaction_t reset = {0};
  reset.length = 8;
  reset.flags = SPI_TRANS_USE_TXDATA;
  reset.tx_data[0] = CC1101_SRES;
  esp_err_t err = spi_device_transmit(s_cc1101_spi, &reset);
  spi_bus_lock_give();
  if (err != ESP_OK)
    ESP_LOGE(TAG, "manual reset failed: %s", esp_err_to_name(err));
  else
    ets_delay_us(1000);
  cc1101_lease_record_error(err);
  return err;
}

static esp_err_t cc1101_txn(spi_transaction_t *t) {
  if (s_cc1101_spi == NULL) {
    cc1101_lease_record_error(ESP_ERR_INVALID_STATE);
    return ESP_ERR_INVALID_STATE;
  }

  if (!spi_bus_lock_take(CC1101_BUS_LOCK_TIMEOUT_MS)) {
    ESP_LOGE(TAG, "SPI3 bus lock timeout");
    cc1101_lease_record_error(ESP_ERR_TIMEOUT);
    return ESP_ERR_TIMEOUT;
  }

  esp_err_t err = spi_device_transmit(s_cc1101_spi, t);
  spi_bus_lock_give();
  if (err != ESP_OK)
    ESP_LOGE(TAG, "SPI transaction failed: %s", esp_err_to_name(err));
  cc1101_lease_record_error(err);
  return err;
}

static esp_err_t write_burst_impl(uint8_t reg, const uint8_t *buf, uint8_t len) {
  if (s_cc1101_spi == NULL)
    return ESP_ERR_INVALID_STATE;
  if (buf == NULL || len == 0)
    return ESP_ERR_INVALID_ARG;

  uint8_t *tx_buf = heap_caps_malloc(len + 1, MALLOC_CAP_DMA);
  if (tx_buf == NULL) {
    ESP_LOGE(TAG, "Cannot allocate burst buffer");
    return ESP_ERR_NO_MEM;
  }

  tx_buf[0] = (reg | 0x40); // Add Burst bit (0x40)
  memcpy(&tx_buf[1], buf, len);

  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.length = (len + 1) * 8;
  t.tx_buffer = tx_buf;
  t.rx_buffer = NULL;

  esp_err_t error = cc1101_txn(&t);
  free(tx_buf);
  return error;
}

static esp_err_t write_reg_impl(uint8_t reg, uint8_t val) {
  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.length = 16;
  t.flags = SPI_TRANS_USE_TXDATA;
  t.tx_data[0] = reg;
  t.tx_data[1] = val;
  return cc1101_txn(&t);
}

static esp_err_t read_register(uint8_t reg, uint8_t *out_value) {
  spi_transaction_t t = {0};
  t.length = 16;
  t.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
  t.tx_data[0] = 0x80 | reg;
  esp_err_t error = cc1101_txn(&t);
  if (error == ESP_OK)
    *out_value = t.rx_data[1];
  return error;
}

static esp_err_t get_register_impl(uint8_t reg, uint8_t *out_value) {
  if (out_value == NULL)
    return ESP_ERR_INVALID_ARG;
  return read_register(reg, out_value);
}

static uint8_t read_reg_impl(uint8_t reg) {
  uint8_t value = 0xFF;
  read_register(reg, &value);
  return value;
}

static void read_burst_impl(uint8_t reg, uint8_t *buf, uint8_t len) {
  if (s_cc1101_spi == NULL || len == 0)
    return;

  uint8_t *tx_buf = heap_caps_malloc(len + 1, MALLOC_CAP_DMA);
  uint8_t *rx_buf = heap_caps_malloc(len + 1, MALLOC_CAP_DMA);
  if (tx_buf == NULL || rx_buf == NULL) {
    if (tx_buf != NULL)
      free(tx_buf);
    if (rx_buf != NULL)
      free(rx_buf);
    cc1101_lease_record_error(ESP_ERR_NO_MEM);
    memset(buf, 0, len);
    return;
  }

  memset(tx_buf, 0, len + 1);
  tx_buf[0] = (reg | 0xC0); // Read (0x80) | Burst (0x40)

  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.length = (len + 1) * 8;
  t.tx_buffer = tx_buf;
  t.rx_buffer = rx_buf;

  if (cc1101_txn(&t) == ESP_OK)
    memcpy(buf, &rx_buf[1], len);
  else
    memset(buf, 0xFF, len);

  free(tx_buf);
  free(rx_buf);
}

static esp_err_t strobe_impl(uint8_t cmd) {
  if (cmd == CC1101_SRES) {
    s_is_tuned = false;
    s_active_preset_id = CC1101_PRESET_IDLE;
    return cc1101_reset_radio();
  }
  if ((cmd == CC1101_STX || cmd == CC1101_SRX) && !s_is_tuned)
    return ESP_ERR_INVALID_STATE;
  spi_transaction_t t = {0};
  t.length = 8;
  t.flags = SPI_TRANS_USE_TXDATA;
  t.tx_data[0] = cmd;
  return cc1101_txn(&t);
}

static esp_err_t calibrate_impl(void) {
  s_is_tuned = false;
  CC1101_RETURN_ON_ERROR(enter_idle());
  uint32_t vco_threshold_hz = CC1101_VCO_HIGH_THRESHOLD_HZ;
  if (s_current_freq_hz <= CC1101_BAND_315_MAX_HZ)
    vco_threshold_hz = CC1101_VCO_315_THRESHOLD_HZ;
  else if (s_current_freq_hz <= CC1101_BAND_433_MAX_HZ)
    vco_threshold_hz = CC1101_VCO_433_THRESHOLD_HZ;
  bool is_high_vco = s_current_freq_hz >= vco_threshold_hz;
  CC1101_RETURN_ON_ERROR(write_reg_impl(CC1101_FSCTRL0, 0));
  uint8_t test0 = is_high_vco ? CC1101_TEST0_HIGH_VCO : CC1101_TEST0_AUTO_VCO;
  CC1101_RETURN_ON_ERROR(write_reg_impl(CC1101_TEST0, test0));
  if (is_high_vco) {
    uint8_t fscal2;
    CC1101_RETURN_ON_ERROR(read_register(CC1101_FSCAL2, &fscal2));
    CC1101_RETURN_ON_ERROR(write_reg_impl(CC1101_FSCAL2, fscal2 | CC1101_FSCAL2_HIGH_VCO));
  }
  CC1101_RETURN_ON_ERROR(strobe_impl(CC1101_SCAL));
  CC1101_RETURN_ON_ERROR(wait_for_state(CC1101_MARCSTATE_IDLE));
  s_is_tuned = true;
  return ESP_OK;
}

static esp_err_t set_frequency_impl(uint32_t freq_hz) {
  cc1101_rf_path_t path = cc1101_rf_path_for_frequency(freq_hz);
  if (path == CC1101_RF_PATH_NONE)
    return ESP_ERR_INVALID_ARG;
  s_is_tuned = false;
  CC1101_RETURN_ON_ERROR(enter_idle());
  uint32_t v1, v2;
  if (!cc1101_rf_path_levels(path, &v1, &v2))
    return ESP_ERR_INVALID_ARG;
  CC1101_RETURN_ON_ERROR(gpio_set_level(GPIO_RF_SW_V1_PIN, v1));
  CC1101_RETURN_ON_ERROR(gpio_set_level(GPIO_RF_SW_V2_PIN, v2));
  ets_delay_us(CC1101_RF_SWITCH_SETTLE_US);

  uint32_t freq_reg = ((uint64_t)freq_hz * CC1101_FREQ_SCALE) / CC1101_XTAL_FREQ_HZ;
  const uint8_t freq_bytes[] = {freq_reg >> 16, freq_reg >> 8, freq_reg};
  CC1101_RETURN_ON_ERROR(write_burst_impl(CC1101_FREQ2, freq_bytes, sizeof(freq_bytes)));
  s_current_freq_hz = freq_hz;
  CC1101_RETURN_ON_ERROR(calibrate_impl());
  esp_err_t error = set_pa_impl(s_current_pa_dbm);
  if (error != ESP_OK)
    s_is_tuned = false;
  return error;
}

static esp_err_t set_rx_bandwidth_impl(float khz) {
  if (!isfinite(khz) || khz <= 0)
    return ESP_ERR_INVALID_ARG;
  float best_error = FLT_MAX;
  uint8_t fields = 0;
  for (uint8_t e = 0; e <= CC1101_BANDWIDTH_EXPONENT_MAX; ++e) {
    for (uint8_t m = 0; m <= CC1101_BANDWIDTH_MANTISSA_MAX; ++m) {
      float divisor = CC1101_KHZ_TO_HZ * CC1101_BANDWIDTH_DIVISOR *
                      (CC1101_BANDWIDTH_MANTISSA_BASE + m) * (1U << e);
      float bandwidth = CC1101_XTAL_FREQ_HZ / divisor;
      float error = fabsf(khz - bandwidth);
      if (error < best_error) {
        best_error = error;
        fields = (e << CC1101_BANDWIDTH_EXPONENT_SHIFT) | (m << CC1101_BANDWIDTH_MANTISSA_SHIFT);
      }
    }
  }
  uint8_t value;
  CC1101_RETURN_ON_ERROR(read_register(CC1101_MDMCFG4, &value));
  return write_reg_impl(CC1101_MDMCFG4, fields | (value & CC1101_RATE_EXPONENT_MASK));
}

static void select_modem_fields(float target,
                                float scale,
                                uint16_t base,
                                uint8_t max_exponent,
                                uint8_t max_mantissa,
                                uint8_t *out_exponent,
                                uint8_t *out_mantissa) {
  float minimum = (float)CC1101_XTAL_FREQ_HZ * base / scale;
  float maximum = (float)CC1101_XTAL_FREQ_HZ * (base + max_mantissa) * (1U << max_exponent) / scale;
  target = fminf(maximum, fmaxf(minimum, target));
  float best_error = FLT_MAX;
  for (uint8_t e = 0; e <= max_exponent; ++e) {
    float step = (float)CC1101_XTAL_FREQ_HZ * (1U << e) / scale;
    float rounded = roundf(target / step - base);
    uint8_t m = (uint8_t)fminf(max_mantissa, fmaxf(0, rounded));
    float error = fabsf(target - (base + m) * step);
    if (error < best_error) {
      best_error = error;
      *out_exponent = e;
      *out_mantissa = m;
    }
  }
}

static esp_err_t set_data_rate_impl(float baud) {
  if (!isfinite(baud) || baud <= 0)
    return ESP_ERR_INVALID_ARG;
  uint8_t e = 0, m = 0, value;
  select_modem_fields(baud,
                      CC1101_RATE_SCALE,
                      CC1101_RATE_MANTISSA_BASE,
                      CC1101_RATE_EXPONENT_MAX,
                      CC1101_MODEM_MANTISSA_MAX,
                      &e,
                      &m);
  CC1101_RETURN_ON_ERROR(read_register(CC1101_MDMCFG4, &value));
  CC1101_RETURN_ON_ERROR(write_reg_impl(CC1101_MDMCFG4, (value & CC1101_BANDWIDTH_MASK) | e));
  return write_reg_impl(CC1101_MDMCFG3, m);
}

static esp_err_t set_deviation_impl(float dev) {
  if (!isfinite(dev) || dev <= 0)
    return ESP_ERR_INVALID_ARG;
  uint8_t e = 0, m = 0;
  select_modem_fields(dev,
                      CC1101_DEVIATION_SCALE * CC1101_KHZ_TO_HZ,
                      CC1101_DEVIATION_MANTISSA_BASE,
                      CC1101_DEVIATION_EXPONENT_MAX,
                      CC1101_DEVIATION_MANTISSA_MAX,
                      &e,
                      &m);
  return write_reg_impl(CC1101_DEVIATN, (e << CC1101_DEVIATION_EXPONENT_SHIFT) | m);
}

static esp_err_t set_modulation_impl(uint8_t modulation) {
  // 0=2-FSK, 1=GFSK, 2=ASK/OOK, 3=4-FSK, 4=MSK
  if (modulation > CC1101_MOD_MSK)
    return ESP_ERR_INVALID_ARG;

  uint8_t m2modfm = 0;
  uint8_t frend0 = 0;

  switch (modulation) {
    case CC1101_MOD_2FSK:
      m2modfm = 0x00;
      frend0 = 0x10;
      break;
    case CC1101_MOD_GFSK:
      m2modfm = 0x10;
      frend0 = 0x10;
      break;
    case CC1101_MOD_ASK:
      m2modfm = 0x30;
      frend0 = 0x11;
      break;
    case CC1101_MOD_4FSK:
      m2modfm = 0x40;
      frend0 = 0x10;
      break;
    case CC1101_MOD_MSK:
      m2modfm = 0x70;
      frend0 = 0x10;
      break;
  }

  uint8_t current_mdmcfg2;
  CC1101_RETURN_ON_ERROR(read_register(CC1101_MDMCFG2, &current_mdmcfg2));
  uint8_t dcoff = current_mdmcfg2 & 0x80;
  uint8_t manch = current_mdmcfg2 & 0x08;
  uint8_t syncm = current_mdmcfg2 & 0x07;

  CC1101_RETURN_ON_ERROR(write_reg_impl(CC1101_MDMCFG2, dcoff | m2modfm | manch | syncm));
  CC1101_RETURN_ON_ERROR(write_reg_impl(CC1101_FREND0, frend0));
  s_current_modulation = modulation;
  return set_pa_impl(s_current_pa_dbm);
}

static esp_err_t set_pa_impl(int dbm) {
  uint8_t pa_val = 0xC0; // Default max

  // Select based on frequency band and dbm
  const uint8_t *table = PA_TABLE_433; // Default
  if (s_current_freq_hz >= CC1101_BAND_315_MIN_HZ && s_current_freq_hz <= CC1101_BAND_315_MAX_HZ)
    table = PA_TABLE_315;
  else if (s_current_freq_hz >= CC1101_BAND_HIGH_MIN_HZ &&
           s_current_freq_hz < CC1101_PATH_915_MIN_HZ)
    table = PA_TABLE_868;
  else if (s_current_freq_hz >= CC1101_PATH_915_MIN_HZ &&
           s_current_freq_hz <= CC1101_BAND_HIGH_MAX_HZ)
    table = PA_TABLE_915;

  if (dbm <= -30)
    pa_val = table[0];
  else if (dbm <= -20)
    pa_val = table[1];
  else if (dbm <= -15)
    pa_val = table[2];
  else if (dbm <= -10)
    pa_val = table[3];
  else if (dbm <= 0)
    pa_val = table[4];
  else if (dbm <= 5)
    pa_val = table[5];
  else if (dbm <= 7)
    pa_val = table[6];
  else
    pa_val = table[7];

  uint8_t pa_table[CC1101_PA_TABLE_SIZE] = {0};

  if (s_current_modulation == CC1101_MOD_ASK) {
    pa_table[0] = 0x00;
    pa_table[1] = pa_val;
  } else {
    pa_table[0] = pa_val;
    pa_table[1] = 0x00;
  }

  CC1101_RETURN_ON_ERROR(write_burst_impl(CC1101_PATABLE, pa_table, sizeof(pa_table)));
  s_current_pa_dbm = dbm;
  return ESP_OK;
}

static void set_channel_impl(uint8_t channel) {
  cc1101_write_reg(CC1101_CHANNR, channel);
}

static esp_err_t set_chsp_impl(float khz) {
  if (!isfinite(khz) || khz <= 0)
    return ESP_ERR_INVALID_ARG;
  uint8_t e = 0, m = 0, value;
  select_modem_fields(khz,
                      CC1101_CHSP_SCALE * CC1101_KHZ_TO_HZ,
                      CC1101_RATE_MANTISSA_BASE,
                      CC1101_CHSP_EXPONENT_MAX,
                      CC1101_MODEM_MANTISSA_MAX,
                      &e,
                      &m);
  CC1101_RETURN_ON_ERROR(read_register(CC1101_MDMCFG1, &value));
  CC1101_RETURN_ON_ERROR(write_reg_impl(CC1101_MDMCFG1, (value & ~CC1101_CHSP_EXPONENT_MASK) | e));
  return write_reg_impl(CC1101_MDMCFG0, m);
}

static void set_sync_mode_impl(uint8_t mode) {
  if (mode > 7)
    mode = 7;
  uint8_t mdmcfg2 = cc1101_read_reg(CC1101_MDMCFG2);
  mdmcfg2 &= 0xF8; // Clear SYNC_MODE (lower 3 bits)
  mdmcfg2 |= mode;
  cc1101_write_reg(CC1101_MDMCFG2, mdmcfg2);
}

static void set_fec_impl(bool enable) {
  uint8_t mdmcfg1 = cc1101_read_reg(CC1101_MDMCFG1);
  if (enable)
    mdmcfg1 |= 0x80;
  else
    mdmcfg1 &= 0x7F;
  cc1101_write_reg(CC1101_MDMCFG1, mdmcfg1);
}

static void set_preamble_impl(uint8_t preamble_bytes) {
  uint8_t val = 0;
  // Map bytes to register value (approximate)
  if (preamble_bytes <= 2)
    val = 0;
  else if (preamble_bytes <= 3)
    val = 1;
  else if (preamble_bytes <= 4)
    val = 2;
  else if (preamble_bytes <= 6)
    val = 3;
  else if (preamble_bytes <= 8)
    val = 4;
  else if (preamble_bytes <= 12)
    val = 5;
  else if (preamble_bytes <= 16)
    val = 6;
  else
    val = 7;

  uint8_t mdmcfg1 = cc1101_read_reg(CC1101_MDMCFG1);
  mdmcfg1 &= 0x8F; // Clear NUM_PREAMBLE (bits 4-6)
  mdmcfg1 |= (val << 4);
  cc1101_write_reg(CC1101_MDMCFG1, mdmcfg1);
}

static void set_dc_filter_off_impl(bool disable) {
  uint8_t mdmcfg2 = cc1101_read_reg(CC1101_MDMCFG2);
  if (disable)
    mdmcfg2 |= 0x80; // Disable DC blocking
  else
    mdmcfg2 &= 0x7F; // Enable DC blocking
  cc1101_write_reg(CC1101_MDMCFG2, mdmcfg2);
}

static void set_manchester_impl(bool enable) {
  uint8_t mdmcfg2 = cc1101_read_reg(CC1101_MDMCFG2);
  if (enable)
    mdmcfg2 |= 0x08;
  else
    mdmcfg2 &= 0xF7;
  cc1101_write_reg(CC1101_MDMCFG2, mdmcfg2);
}

static esp_err_t send_data_impl(const uint8_t *data, size_t len) {
  if (data == NULL || len == 0 || len > CC1101_MAX_TX_FIFO)
    return ESP_ERR_INVALID_ARG;
  uint8_t packet_control;
  CC1101_RETURN_ON_ERROR(read_register(CC1101_PKTCTRL0, &packet_control));
  if ((packet_control & CC1101_PACKET_FORMAT_MASK) != 0 ||
      (packet_control & CC1101_PACKET_LENGTH_MASK) != CC1101_PACKET_VARIABLE_LENGTH)
    return ESP_ERR_NOT_SUPPORTED;
  esp_err_t cleanup_error;
  esp_err_t error = enter_idle();
  if (error != ESP_OK)
    return error;
  error = strobe_impl(CC1101_SFTX);
  if (error != ESP_OK)
    goto cleanup;
  error = write_reg_impl(CC1101_TXFIFO, (uint8_t)len);
  if (error != ESP_OK)
    goto cleanup;
  error = write_burst_impl(CC1101_TXFIFO, data, (uint8_t)len);
  if (error != ESP_OK)
    goto cleanup;
  error = strobe_impl(CC1101_STX);
  if (error != ESP_OK)
    goto cleanup;
  int64_t deadline = esp_timer_get_time() + CC1101_PACKET_TX_TIMEOUT_US;
  do {
    uint8_t state, remaining;
    error = read_register(CC1101_MARCSTATE | CC1101_STATUS_READ, &state);
    if (error != ESP_OK)
      goto cleanup;
    error = read_register(CC1101_TXBYTES | CC1101_STATUS_READ, &remaining);
    if (error != ESP_OK)
      goto cleanup;
    if (remaining & CC1101_TX_FIFO_UNDERFLOW) {
      error = ESP_FAIL;
      goto cleanup;
    }
    state &= CC1101_MARCSTATE_MASK;
    if ((remaining & CC1101_TX_FIFO_COUNT_MASK) == 0 &&
        (state == CC1101_MARCSTATE_IDLE || state == CC1101_MARCSTATE_RX ||
         state == CC1101_MARCSTATE_FSTXON))
      goto cleanup;
    vTaskDelay(1);
  } while (esp_timer_get_time() < deadline);
  error = ESP_ERR_TIMEOUT;
cleanup:
  cleanup_error = enter_idle();
  if (cleanup_error == ESP_OK)
    cleanup_error = strobe_impl(CC1101_SFTX);
  if (error == ESP_OK)
    error = cleanup_error;
  if (error != ESP_OK)
    ESP_LOGE(TAG, "Packet transmission failed: %s", esp_err_to_name(error));
  return error;
}

float cc1101_convert_rssi(uint8_t rssi_raw) {
  int16_t signed_rssi = rssi_raw >= CC1101_RSSI_SIGN_THRESHOLD
                            ? (int16_t)rssi_raw - CC1101_RSSI_SIGN_RANGE
                            : rssi_raw;
  return signed_rssi * CC1101_RSSI_DB_PER_STEP - CC1101_RSSI_OFFSET;
}

static esp_err_t init_impl(void) {
  if (s_is_initialized)
    return ESP_OK;
  s_is_tuned = false;
  if (s_cc1101_spi == NULL) {
    const gpio_config_t rf_switch_cfg = {
        .pin_bit_mask = (1ULL << GPIO_RF_SW_V1_PIN) | (1ULL << GPIO_RF_SW_V2_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    CC1101_RETURN_ON_ERROR(gpio_config(&rf_switch_cfg));
    CC1101_RETURN_ON_ERROR(gpio_set_level(GPIO_RF_SW_V1_PIN, 0));
    CC1101_RETURN_ON_ERROR(gpio_set_level(GPIO_RF_SW_V2_PIN, 0));
    const spi_device_config_t devcfg = {
        .clock_speed_hz = CC1101_SPI_CLOCK_HZ,
        .mode = 0,
        .cs_pin = GPIO_CC1101_CS_PIN,
        .queue_size = CC1101_SPI_QUEUE_SIZE,
    };
    CC1101_RETURN_ON_ERROR(spi_add_device(SPI3_HOST, SPI_DEVICE_CC1101, &devcfg));
    s_cc1101_spi = spi_get_handle(SPI_DEVICE_CC1101);
  }
  CC1101_RETURN_ON_ERROR(strobe_impl(CC1101_SRES));
  CC1101_RETURN_ON_ERROR(wait_for_state(CC1101_MARCSTATE_IDLE));
  uint8_t part, version;
  CC1101_RETURN_ON_ERROR(read_register(CC1101_PARTNUM | CC1101_STATUS_READ, &part));
  CC1101_RETURN_ON_ERROR(read_register(CC1101_VERSION | CC1101_STATUS_READ, &version));
  if (part != 0 || version == 0 || version == UINT8_MAX) {
    ESP_LOGE(TAG, "CC1101 not detected");
    return ESP_ERR_NOT_FOUND;
  }
  CC1101_RETURN_ON_ERROR(write_reg_impl(CC1101_IOCFG0, CC1101_GDO_HIGH_IMPEDANCE));
  CC1101_RETURN_ON_ERROR(write_reg_impl(CC1101_IOCFG2, CC1101_GDO_HIGH_IMPEDANCE));
  CC1101_RETURN_ON_ERROR(set_modulation_impl(CC1101_MOD_ASK));
  CC1101_RETURN_ON_ERROR(set_frequency_impl(CC1101_DEFAULT_FREQ_HZ));
  s_is_initialized = true;
  ESP_LOGI(TAG, "CC1101 initialized, version 0x%02X", version);
  return ESP_OK;
}

static bool is_initialized_impl(void) {
  return s_is_initialized;
}

static esp_err_t recover_impl(void) {
  s_is_initialized = false;
  return init_impl();
}

static esp_err_t enable_async_mode_impl(uint32_t freq_hz) {
  return configure_sniffer(freq_hz, cc1101_config_get_profile(CC1101_PRESET_OOK_800KHZ));
}

static esp_err_t enable_fsk_mode_impl(uint32_t freq_hz) {
  return configure_sniffer(freq_hz, cc1101_config_get_profile(CC1101_PRESET_IDLE));
}

static esp_err_t set_preset_impl(cc1101_preset_t preset, uint32_t freq_hz) {
  if (preset < CC1101_PRESET_IDLE || preset >= CC1101_PRESET_COUNT)
    return ESP_ERR_INVALID_ARG;
  if (preset == CC1101_PRESET_IDLE) {
    CC1101_RETURN_ON_ERROR(enter_idle());
    s_active_preset_id = CC1101_PRESET_IDLE;
    return ESP_OK;
  }
  CC1101_RETURN_ON_ERROR(configure_sniffer(freq_hz, cc1101_config_get_profile(preset)));
  s_active_preset_id = (uint8_t)preset;
  return ESP_OK;
}

static uint8_t get_active_preset_id_impl(void) {
  return s_active_preset_id;
}

static esp_err_t set_radio_config_impl(const cc1101_radio_config_t *config, uint32_t freq_hz) {
  CC1101_RETURN_ON_ERROR(cc1101_config_validate(config));
  cc1101_async_profile_t profile = *cc1101_config_get_profile(config->preset);
  profile.values[CC1101_PROFILE_MDMCFG4] = config->mdmcfg4;
  profile.values[CC1101_PROFILE_MDMCFG3] = config->mdmcfg3;
  profile.values[CC1101_PROFILE_DEVIATN] = config->deviatn;
  profile.values[CC1101_PROFILE_AGCCTRL2] = config->agcctrl2;
  CC1101_RETURN_ON_ERROR(configure_sniffer(freq_hz, &profile));
  s_active_preset_id = config->preset;
  return ESP_OK;
}

static esp_err_t get_radio_config_impl(cc1101_radio_config_t *out_config) {
  if (out_config == NULL)
    return ESP_ERR_INVALID_ARG;
  if (!s_is_tuned || s_active_preset_id == CC1101_PRESET_IDLE)
    return ESP_ERR_INVALID_STATE;
  cc1101_radio_config_t config = {.preset = s_active_preset_id};
  CC1101_RETURN_ON_ERROR(read_register(CC1101_MDMCFG4, &config.mdmcfg4));
  CC1101_RETURN_ON_ERROR(read_register(CC1101_MDMCFG3, &config.mdmcfg3));
  CC1101_RETURN_ON_ERROR(read_register(CC1101_DEVIATN, &config.deviatn));
  CC1101_RETURN_ON_ERROR(read_register(CC1101_AGCCTRL2, &config.agcctrl2));
  CC1101_RETURN_ON_ERROR(cc1101_config_validate(&config));
  *out_config = config;
  return ESP_OK;
}

static esp_err_t enter_mode(uint8_t command, uint8_t state) {
  CC1101_RETURN_ON_ERROR(enter_idle());
  esp_err_t error = strobe_impl(command);
  if (error == ESP_OK)
    error = wait_for_state(state);
  if (error != ESP_OK) {
    s_is_tuned = false;
    strobe_impl(CC1101_SIDLE);
  }
  return error;
}

static esp_err_t enter_tx_mode_impl(void) {
  return enter_mode(CC1101_STX, CC1101_MARCSTATE_TX);
}

static esp_err_t enter_rx_mode_impl(void) {
  return enter_mode(CC1101_SRX, CC1101_MARCSTATE_RX);
}
