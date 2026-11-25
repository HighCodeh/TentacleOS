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

#include "radio_platform.h"
#include "../../firmware_p4/components/Drivers/cc1101/cc1101.c"

static uint8_t s_registers[256], s_pa[8], s_state;
static uint32_t s_v1, s_v2;
static unsigned s_transactions, s_fail_transaction, s_calibration_polls, s_rx_count;
static unsigned s_gpio_writes, s_fail_gpio;
static int64_t s_time_us;
static bool s_is_idle_stuck, s_is_calibration_stuck, s_is_rx_stuck;
static bool s_is_lock_failed, s_is_allocation_failed, s_is_gpio_config_failed;
static bool s_is_attach_failed, s_is_chip_missing, s_has_calibrated;
static esp_err_t s_error;
static bool s_is_packet_stuck, s_is_packet_underflow;
static unsigned s_packet_polls;
static unsigned s_lock_count, s_unlock_count;
static bool s_bus_locked;
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (TaskHandle_t)1; }

bool cc1101_lease_enter(void) { return true; }
void cc1101_lease_leave(void) {}
void cc1101_lease_record_error(esp_err_t error) {
  if (s_error == ESP_OK) s_error = error;
}
int64_t esp_timer_get_time(void) { return s_time_us; }
void ets_delay_us(uint32_t delay) { s_time_us += delay; }
void vTaskDelay(TickType_t ticks) { s_time_us += ticks * 1000; }
void *heap_caps_malloc(size_t size, unsigned caps) {
  assert(caps == MALLOC_CAP_DMA);
  return s_is_allocation_failed ? NULL : malloc(size);
}
bool spi_bus_lock_take(uint32_t timeout) {
  assert(!s_bus_locked);
  s_lock_count++;
  if (s_is_lock_failed) return false;
  s_bus_locked = true; return true;
}
void spi_bus_lock_give(void) { assert(s_bus_locked); s_bus_locked = false; s_unlock_count++; }
esp_err_t spi_device_polling_transmit(spi_device_handle_t handle, spi_transaction_t *transaction) {
  assert(s_bus_locked && atomic_load(&s_scan_bus_owner) == xTaskGetCurrentTaskHandle());
  return spi_device_transmit(handle, transaction);
}
esp_err_t gpio_config(const gpio_config_t *config) {
  assert(config->pin_bit_mask == ((1ULL << 17) | (1ULL << 18)));
  return s_is_gpio_config_failed ? ESP_FAIL : ESP_OK;
}
esp_err_t gpio_set_level(int pin, uint32_t level) {
  assert(s_state == CC1101_MARCSTATE_IDLE);
  assert(level <= 1);
  if (++s_gpio_writes == s_fail_gpio) return ESP_FAIL;
  if (pin == 17) s_v1 = level;
  else { assert(pin == 18); s_v2 = level; }
  return ESP_OK;
}
esp_err_t spi_add_device(int host, int id, const spi_device_config_t *config) {
  assert(host == SPI3_HOST && id == SPI_DEVICE_CC1101 && config->clock_speed_hz == 1000000);
  return s_is_attach_failed ? ESP_FAIL : ESP_OK;
}
spi_device_handle_t spi_get_handle(int id) { return (void *)1; }

esp_err_t spi_device_transmit(spi_device_handle_t handle, spi_transaction_t *transaction) {
  assert(handle != NULL);
  if (++s_transactions == s_fail_transaction) return ESP_FAIL;
  const uint8_t *tx = transaction->flags & SPI_TRANS_USE_TXDATA
                          ? transaction->tx_data : transaction->tx_buffer;
  size_t length = transaction->length / 8;
  uint8_t reg = tx[0] & 0x3F;
  if (tx[0] & 0x80) {
    assert(length == 2 && transaction->flags & SPI_TRANS_USE_RXDATA);
    if (reg == CC1101_MARCSTATE) {
      if (s_packet_polls && !s_is_packet_stuck && !s_is_packet_underflow && --s_packet_polls == 0) {
        s_state = CC1101_MARCSTATE_IDLE;
        s_registers[CC1101_TXBYTES] = 0;
      }
      if (s_calibration_polls && !s_is_calibration_stuck && --s_calibration_polls == 0) {
        s_state = CC1101_MARCSTATE_IDLE;
        s_registers[CC1101_FSCAL3] = 0xEA;
        s_registers[CC1101_FSCAL2] = (s_registers[CC1101_FSCAL2] & 0x20) | 0x15;
        s_registers[CC1101_FSCAL1] = 0x2B;
        s_has_calibrated = true;
      }
      transaction->rx_data[1] = s_state;
    } else if (reg == CC1101_VERSION) {
      transaction->rx_data[1] = s_is_chip_missing ? 0xFF : 0x14;
    } else {
      transaction->rx_data[1] = s_registers[reg];
    }
  } else if (length == 1) {
    switch (reg) {
      case CC1101_SRES:
        memset(s_registers, 0, sizeof(s_registers));
        s_registers[CC1101_MDMCFG2] = 0x02;
        s_registers[CC1101_FSCAL2] = 0x0A;
        s_state = CC1101_MARCSTATE_IDLE;
        s_has_calibrated = false;
        break;
      case CC1101_SIDLE:
        if (!s_is_idle_stuck) s_state = CC1101_MARCSTATE_IDLE;
        break;
      case CC1101_SCAL:
        assert(s_state == CC1101_MARCSTATE_IDLE);
        if (s_registers[CC1101_TEST0] == 0x09)
          assert(s_registers[CC1101_FSCAL2] & 0x20);
        s_state = 0x08;
        s_calibration_polls = 3;
        s_has_calibrated = false;
        break;
      case CC1101_SRX:
        assert(s_has_calibrated);
        ++s_rx_count;
        s_state = s_is_rx_stuck ? 0x08 : CC1101_MARCSTATE_RX;
        break;
      case CC1101_STX:
        assert(s_has_calibrated);
        s_state = CC1101_MARCSTATE_TX;
        if (s_registers[CC1101_TXBYTES]) {
          s_packet_polls = 3;
          if (s_is_packet_underflow) s_registers[CC1101_TXBYTES] = 0x80;
        }
        break;
      case CC1101_SFTX:
        s_registers[CC1101_TXBYTES] = 0;
        s_packet_polls = 0;
        break;
      default: break;
    }
  } else {
    if (reg == CC1101_TXFIFO) {
      assert(s_state == CC1101_MARCSTATE_IDLE);
      s_registers[CC1101_TXBYTES] += length - 1;
    } else if (reg == CC1101_PATABLE) {
      assert(length == 9);
      memcpy(s_pa, tx + 1, sizeof(s_pa));
    } else {
      assert(s_state == CC1101_MARCSTATE_IDLE);
      for (size_t i = 1; i < length; ++i) s_registers[reg + i - 1] = tx[i];
    }
  }
  return ESP_OK;
}

static void reset_test(void) {
  assert(!s_bus_locked && atomic_load(&s_scan_bus_owner) == NULL);
  s_lock_count = s_unlock_count = 0;
  memset(s_registers, 0, sizeof(s_registers));
  memset(s_pa, 0, sizeof(s_pa));
  s_cc1101_spi = (void *)1;
  s_is_initialized = true;
  s_is_tuned = false;
  s_current_freq_hz = 433920000;
  s_current_modulation = CC1101_MOD_ASK;
  s_current_pa_dbm = 10;
  s_active_preset_id = 0;
  s_state = CC1101_MARCSTATE_IDLE;
  s_v1 = s_v2 = s_transactions = s_fail_transaction = s_calibration_polls = s_rx_count = 0;
  s_gpio_writes = s_fail_gpio = 0;
  s_time_us = 0;
  s_is_idle_stuck = s_is_calibration_stuck = s_is_rx_stuck = false;
  s_is_lock_failed = s_is_allocation_failed = s_is_gpio_config_failed = false;
  s_is_attach_failed = s_is_chip_missing = s_has_calibrated = false;
  s_error = ESP_OK;
  s_is_packet_stuck = s_is_packet_underflow = false;
  s_packet_polls = 0;
}

static void test_paths(void) {
  const struct { uint32_t frequency; unsigned path, v1, v2; uint8_t pa; } cases[] = {
      {300000000, 1, 0, 0, 0xC2}, {315000000, 1, 0, 0, 0xC2}, {348000000, 1, 0, 0, 0xC2},
      {387000000, 2, 0, 1, 0xC0}, {433920000, 2, 0, 1, 0xC0}, {464000000, 2, 0, 1, 0xC0},
      {779000000, 4, 1, 1, 0xC5}, {868350000, 4, 1, 1, 0xC5}, {899999999, 4, 1, 1, 0xC5},
      {900000000, 2, 0, 1, 0xC3}, {915000000, 2, 0, 1, 0xC3}, {928000000, 2, 0, 1, 0xC3},
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    reset_test();
    s_state = CC1101_MARCSTATE_TX;
    assert(cc1101_rf_path_for_frequency(cases[i].frequency) == cases[i].path);
    cc1101_set_frequency(cases[i].frequency);
    assert(s_error == ESP_OK && s_is_tuned && s_has_calibrated);
    assert(s_state == CC1101_MARCSTATE_IDLE && s_v1 == cases[i].v1 && s_v2 == cases[i].v2);
    uint32_t word = (s_registers[CC1101_FREQ2] << 16) | (s_registers[CC1101_FREQ1] << 8) |
                    s_registers[CC1101_FREQ0];
    double actual = word * 26000000.0 / 65536.0;
    assert(actual <= cases[i].frequency && cases[i].frequency - actual < 397.0);
    assert(s_pa[0] == 0 && s_pa[1] == cases[i].pa);
    assert(s_registers[CC1101_FSCTRL0] == 0);
    cc1101_set_modulation(CC1101_MOD_2FSK);
    assert(s_error == ESP_OK && s_pa[0] == cases[i].pa && s_pa[1] == 0);
  }
  const uint32_t invalid[] = {0, 299999999, 348000001, 386999999, 464000001, 778999999,
                              928000001, UINT32_MAX};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    reset_test();
    cc1101_set_frequency(invalid[i]);
    assert(s_error == ESP_ERR_INVALID_ARG && s_transactions == 0 && s_gpio_writes == 0);
  }
  uint32_t a, b;
  assert(!cc1101_rf_path_levels(CC1101_RF_PATH_NONE, &a, &b));
  assert(!cc1101_rf_path_levels(CC1101_RF_PATH_COUNT, &a, &b));
  assert(!cc1101_rf_path_levels(CC1101_RF_PATH_1, NULL, &b));
  assert(!cc1101_rf_path_levels(CC1101_RF_PATH_1, &a, NULL));
}

static void test_power_levels(void) {
  /* TI SWRS061I tables 37 (868/915) and 39 (315/433), not generated by the driver. */
  const uint32_t frequencies[] = {315000000, 433920000, 868350000, 915000000};
  const int levels[] = {-30, -20, -15, -10, 0, 5, 7, 10};
  const uint8_t expected[][8] = {
      {0x12, 0x0D, 0x1C, 0x34, 0x51, 0x85, 0xCB, 0xC2},
      {0x12, 0x0E, 0x1D, 0x34, 0x60, 0x84, 0xC8, 0xC0},
      {0x03, 0x17, 0x1D, 0x26, 0x50, 0x86, 0xCD, 0xC5},
      {0x03, 0x0E, 0x1E, 0x27, 0x8E, 0x84, 0xCC, 0xC3},
  };
  for (size_t band = 0; band < 4; ++band) {
    reset_test();
    assert(cc1101_set_frequency(frequencies[band]) == ESP_OK);
    for (size_t level = 0; level < 8; ++level) {
      assert(cc1101_set_modulation(CC1101_MOD_ASK) == ESP_OK);
      assert(cc1101_set_pa(levels[level]) == ESP_OK);
      assert(s_pa[0] == 0 && s_pa[1] == expected[band][level]);
      assert(cc1101_set_modulation(CC1101_MOD_2FSK) == ESP_OK);
      assert(s_pa[0] == expected[band][level] && s_pa[1] == 0);
    }
  }
}

static void test_presets(void) {
  const uint8_t bandwidth[] = {0, 0x60, 0x10, 0x60, 0x60, 0x20, 0x00};
  const uint8_t deviation[] = {0, 0x47, 0x47, 0x04, 0x47, 0x57, 0x47};
  for (int preset = 1; preset < CC1101_PRESET_COUNT; ++preset) {
    reset_test();
    cc1101_set_preset(preset, 433920000);
    assert(s_error == ESP_OK && s_active_preset_id == preset && s_rx_count == 1);
    assert(s_state == CC1101_MARCSTATE_RX && s_registers[CC1101_FSCAL1] == 0x2B);
    assert(s_registers[CC1101_FSCAL3] == 0xEA && s_registers[CC1101_TEST0] == 0x09);
    assert((s_registers[CC1101_MDMCFG4] & 0xF0) == bandwidth[preset]);
    assert(s_registers[CC1101_DEVIATN] == deviation[preset]);
    bool is_reference_fsk = preset == 3 || preset == 4;
    assert((s_registers[CC1101_MDMCFG2] & 7) == (is_reference_fsk ? 4 : 0));
    bool is_fsk = preset >= 3 && preset <= 5;
    assert(s_registers[CC1101_FOCCFG] == (is_fsk ? 0x16 : preset == 6 ? 0x14 : 0x18));
    assert(s_registers[CC1101_AGCCTRL2] == (preset == 1 ? 0x03 : preset == 5 ? 0xC7 : 0x07));
    assert(s_registers[CC1101_AGCCTRL0] == (preset == 1 ? 0x40 : 0x91));
    assert(s_registers[CC1101_FIFOTHR] == (preset == 1 ? 0x47 : 0x07));
    assert(s_registers[CC1101_WORCTRL] == 0xFB);
    assert(s_registers[CC1101_PKTCTRL1] == (is_reference_fsk ? 0x04 : 0x00));
    assert(s_registers[CC1101_MDMCFG0] == (preset <= 4 ? 0x00 : 0xF8));
    assert(s_registers[CC1101_MDMCFG1] == (preset <= 2 ? 0x00 : 0x02));
    assert(s_registers[CC1101_BSCFG] == (preset <= 4 ? 0x6C : 0x1C));
    assert(s_registers[CC1101_FREND1] == (is_fsk ? 0x56 : 0xB6));
    double rate = (256 + s_registers[CC1101_MDMCFG3]) *
                  (1U << (s_registers[CC1101_MDMCFG4] & 15)) * 26000000.0 / 268435456.0;
    assert(fabs(rate - (preset <= 2 ? 3793.716 : 4797.94)) < 0.02);
    cc1101_enter_tx_mode();
    assert(s_error == ESP_OK && s_state == CC1101_MARCSTATE_TX);
    cc1101_set_preset(CC1101_PRESET_IDLE, 0);
    assert(s_state == CC1101_MARCSTATE_IDLE && s_active_preset_id == 0);
  }
  reset_test();
  cc1101_set_preset(CC1101_PRESET_COUNT, 433920000);
  assert(s_error == ESP_ERR_INVALID_ARG && s_transactions == 0);
  reset_test();
  cc1101_set_preset(CC1101_PRESET_OOK_270KHZ, 315000000);
  assert(s_error == ESP_OK && s_registers[CC1101_TEST0] == 0x0B);
  cc1101_set_preset(CC1101_PRESET_OOK_270KHZ, 868350000);
  assert(s_error == ESP_OK && s_registers[CC1101_TEST0] == 0x09 && s_v1 == 1 && s_v2 == 1);
}

static void test_math(void) {
  reset_test();
  s_registers[CC1101_MDMCFG4] = 0x0B;
  cc1101_set_rx_bandwidth(100);
  assert(s_registers[CC1101_MDMCFG4] == 0xCB);
  s_registers[CC1101_MDMCFG1] = 0xAC;
  cc1101_set_chsp(200);
  assert(s_error == ESP_OK && (s_registers[CC1101_MDMCFG1] & 0xFC) == 0xAC);
  double spacing = (256 + s_registers[CC1101_MDMCFG0]) *
                   (1U << (s_registers[CC1101_MDMCFG1] & 3)) * 26000000.0 / 262144.0;
  assert(fabs(spacing - 200000) < 200);
  for (unsigned e = 0; e < 15; ++e) {
    float target = 511.75f * (1U << e) * 26000000.0f / 268435456.0f;
    cc1101_set_data_rate(target);
    assert((s_registers[CC1101_MDMCFG4] & 15) == e + 1);
    assert(s_registers[CC1101_MDMCFG3] == 0);
  }
  cc1101_set_data_rate(FLT_MAX);
  assert((s_registers[CC1101_MDMCFG4] & 15) == 15 && s_registers[CC1101_MDMCFG3] == 255);
  cc1101_set_data_rate(FLT_MIN);
  assert((s_registers[CC1101_MDMCFG4] & 15) == 0 && s_registers[CC1101_MDMCFG3] == 0);
  cc1101_set_deviation(FLT_MAX);
  assert(s_registers[CC1101_DEVIATN] == 0x77);
  cc1101_set_deviation(FLT_MIN);
  assert(s_registers[CC1101_DEVIATN] == 0);
  cc1101_set_chsp(FLT_MAX);
  assert((s_registers[CC1101_MDMCFG1] & 3) == 3 && s_registers[CC1101_MDMCFG0] == 255);
  cc1101_set_chsp(FLT_MIN);
  assert((s_registers[CC1101_MDMCFG1] & 3) == 0 && s_registers[CC1101_MDMCFG0] == 0);
  cc1101_set_rx_bandwidth(FLT_MAX);
  assert((s_registers[CC1101_MDMCFG4] & 0xF0) == 0);
  cc1101_set_rx_bandwidth(FLT_MIN);
  assert((s_registers[CC1101_MDMCFG4] & 0xF0) == 0xF0);
  const float invalid[] = {0, -1, NAN, INFINITY};
  esp_err_t (*setters[])(float) = {cc1101_set_rx_bandwidth, cc1101_set_data_rate,
                            cc1101_set_deviation, cc1101_set_chsp};
  for (size_t i = 0; i < sizeof(setters) / sizeof(setters[0]); ++i) {
    for (size_t j = 0; j < sizeof(invalid) / sizeof(invalid[0]); ++j) {
      reset_test();
      setters[i](invalid[j]);
      assert(s_error == ESP_ERR_INVALID_ARG && s_transactions == 0);
    }
  }
}

static void test_vco_thresholds(void) {
  const uint32_t thresholds[] = {322880000, 430500000, 861000000};
  for (size_t i = 0; i < sizeof(thresholds) / sizeof(thresholds[0]); ++i) {
    reset_test();
    cc1101_set_frequency(thresholds[i]);
    assert(s_error == ESP_OK && s_registers[CC1101_TEST0] == 0x09);
    assert(s_registers[CC1101_FSCAL2] & 0x20);
    cc1101_set_frequency(thresholds[i] - 1);
    assert(s_error == ESP_OK && s_registers[CC1101_TEST0] == 0x0B);
  }
}

static void test_failures(void) {
  reset_test();
  cc1101_set_preset(CC1101_PRESET_OOK_270KHZ, 433920000);
  unsigned total = s_transactions;
  for (unsigned failure = 1; failure <= total; ++failure) {
    reset_test();
    s_fail_transaction = failure;
    cc1101_set_preset(CC1101_PRESET_OOK_270KHZ, 433920000);
    assert(s_error == ESP_FAIL && !s_is_tuned && s_active_preset_id == 0);
    assert(s_state != CC1101_MARCSTATE_RX && s_state != CC1101_MARCSTATE_TX);
    unsigned rx_count = s_rx_count;
    cc1101_enter_rx_mode();
    cc1101_enter_tx_mode();
    assert(s_rx_count == rx_count && s_state != CC1101_MARCSTATE_TX);
  }
  reset_test(); s_state = CC1101_MARCSTATE_RX; s_is_idle_stuck = true;
  cc1101_set_frequency(868350000);
  assert(s_error == ESP_ERR_TIMEOUT && s_gpio_writes == 0 && s_time_us <= 101000);
  reset_test(); s_is_calibration_stuck = true;
  cc1101_set_preset(CC1101_PRESET_OOK_270KHZ, 868350000);
  assert(s_error == ESP_ERR_TIMEOUT && !s_is_tuned && s_rx_count == 0 && s_time_us < 110000);
  reset_test(); s_is_rx_stuck = true;
  cc1101_set_preset(CC1101_PRESET_OOK_270KHZ, 868350000);
  assert(s_error == ESP_ERR_TIMEOUT && !s_is_tuned && s_state == CC1101_MARCSTATE_IDLE);
  reset_test(); s_is_lock_failed = true;
  cc1101_set_frequency(433920000);
  assert(s_error == ESP_ERR_TIMEOUT && s_transactions == 0 && s_gpio_writes == 0);
  for (unsigned pin = 1; pin <= 2; ++pin) {
    reset_test(); s_fail_gpio = pin;
    cc1101_set_frequency(433920000);
    assert(s_error == ESP_FAIL && !s_is_tuned && !s_has_calibrated);
  }
  reset_test(); s_is_allocation_failed = true;
  cc1101_set_frequency(433920000);
  assert(s_error == ESP_ERR_NO_MEM && !s_is_tuned && !s_has_calibrated);
}

static void test_initialization(void) {
  for (unsigned failure = 0; failure < 4; ++failure) {
    reset_test(); s_cc1101_spi = NULL; s_is_initialized = false;
    s_is_gpio_config_failed = failure == 1;
    s_is_attach_failed = failure == 2;
    s_is_chip_missing = failure == 3;
    cc1101_init();
    assert(cc1101_is_initialized() == (failure == 0));
    assert((s_error == ESP_OK) == (failure == 0));
    s_is_gpio_config_failed = s_is_attach_failed = s_is_chip_missing = false;
    s_error = ESP_OK;
    cc1101_init();
    assert(s_error == ESP_OK && cc1101_is_initialized() && s_is_tuned);
    s_is_chip_missing = true;
    assert(cc1101_recover() == ESP_ERR_NOT_FOUND && !cc1101_is_initialized());
  }
}

static void test_packets(void) {
  const uint8_t packet[] = {1, 2, 3};
  reset_test();
  assert(cc1101_set_frequency(433920000) == ESP_OK);
  assert(cc1101_send_data(NULL, 3) == ESP_ERR_INVALID_ARG);
  assert(cc1101_send_data(packet, 0) == ESP_ERR_INVALID_ARG);
  assert(cc1101_send_data(packet, 62) == ESP_ERR_INVALID_ARG);
  s_registers[CC1101_PKTCTRL0] = 0x32;
  assert(cc1101_send_data(packet, sizeof(packet)) == ESP_ERR_NOT_SUPPORTED);
  s_registers[CC1101_PKTCTRL0] = 1;
  s_error = ESP_OK;
  unsigned before = s_transactions;
  assert(cc1101_send_data(packet, sizeof(packet)) == ESP_OK);
  unsigned transfers = s_transactions - before;
  assert(s_state == CC1101_MARCSTATE_IDLE && s_registers[CC1101_TXBYTES] == 0);
  for (unsigned i = 1; i <= transfers; ++i) {
    reset_test();
    assert(cc1101_set_frequency(433920000) == ESP_OK);
    s_registers[CC1101_PKTCTRL0] = 1;
    s_fail_transaction = s_transactions + i;
    assert(cc1101_send_data(packet, sizeof(packet)) == ESP_FAIL);
  }
  reset_test();
  assert(cc1101_set_frequency(433920000) == ESP_OK);
  s_registers[CC1101_PKTCTRL0] = 1;
  s_is_packet_stuck = true;
  int64_t before_time = s_time_us;
  assert(cc1101_send_data(packet, sizeof(packet)) == ESP_ERR_TIMEOUT);
  assert(s_time_us - before_time >= 2000000 && s_time_us - before_time <= 2001000);
  assert(s_state == CC1101_MARCSTATE_IDLE && s_registers[CC1101_TXBYTES] == 0);
  s_is_packet_stuck = false;
  s_is_packet_underflow = true;
  assert(cc1101_send_data(packet, sizeof(packet)) == ESP_FAIL);
  assert(s_state == CC1101_MARCSTATE_IDLE && s_registers[CC1101_TXBYTES] == 0);
  uint8_t value = 42;
  s_fail_transaction = s_transactions + 1;
  assert(cc1101_get_register(CC1101_RSSI | 0x40, &value) == ESP_FAIL && value == 42);
  assert(cc1101_get_register(CC1101_RSSI, NULL) == ESP_ERR_INVALID_ARG);
}

static void test_scan_sample(void) {
  reset_test();
  float rssi = 42;
  s_registers[CC1101_RSSI] = 200;
  assert(cc1101_scan_rssi(315000000, &rssi) == ESP_OK);
  assert(rssi == -102 && s_state == CC1101_MARCSTATE_RX);
  assert(s_lock_count == 1 && s_unlock_count == 1 && !s_bus_locked);
  unsigned total = s_transactions;
  for (unsigned failure = 1; failure <= total; failure++) {
    reset_test(); rssi = 42; s_fail_transaction = failure;
    assert(cc1101_scan_rssi(315000000, &rssi) != ESP_OK && rssi == 42);
    assert(s_lock_count == 1 && s_unlock_count == 1 && !s_bus_locked);
  }
  reset_test(); s_is_lock_failed = true;
  assert(cc1101_scan_rssi(315000000, &rssi) == ESP_ERR_TIMEOUT && !s_unlock_count);
  reset_test(); s_is_calibration_stuck = true;
  assert(cc1101_scan_rssi(315000000, &rssi) == ESP_ERR_TIMEOUT && !s_bus_locked);
  assert(cc1101_scan_rssi(500000000, &rssi) == ESP_ERR_INVALID_ARG);
  assert(cc1101_scan_rssi(315000000, NULL) == ESP_ERR_INVALID_ARG);
}

int main(void) {
  for (unsigned raw = 0; raw <= UINT8_MAX; ++raw) {
    float expected = (raw >= 128 ? (int)raw - 256 : (int)raw) / 2.0f - 74.0f;
    assert(cc1101_convert_rssi(raw) == expected);
  }
  test_paths();
  test_power_levels();
  test_presets();
  test_math();
  test_vco_thresholds();
  test_failures();
  test_initialization();
  test_packets();
  test_scan_sample();
  puts("PASS: production CC1101 band-specific path selection, modem math, preset ordering, timeouts, SPI/GPIO failures and recovery");
}
