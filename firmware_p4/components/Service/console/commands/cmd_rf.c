// Copyright (c) 2025 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "console_service.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cc1101.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_console.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "highboy_nfc.h"
#include "lf_rfid.h"
#include "lora_lab.h"
#include "lora_session.h"
#include "mf_classic_emu.h"
#include "nfc_debug.h"
#include "nfc_listener.h"
#include "nfc_poller.h"
#include "nfc_scanner.h"
#include "pin_def.h"
#include "poller.h"
#include "power_manager.h"
#include "rf_frontend.h"
#include "st25r3916_core.h"
#include "subghz_protocol_registry.h"
#include "subghz_receiver.h"
#include "subghz_transmitter.h"
#include "sx1262.h"
#include "sx1262_cmd.h"
#include "sx1262_hal.h"
#include "sx1262_regs.h"

#define LF_DEFAULT_MEASURE_MS    50U
#define LF_DEFAULT_READ_MS       150U
#define LF_DEFAULT_WATCH_S       10U
#define LF_MAX_WATCH_S           60U
#define LF_MOD_TEST_RATE_HZ      1953U
#define LF_MOD_TEST_MS           500U
#define NFC_MEASURE_DELAY_MS     10U
#define CC1101_DEFAULT_TEST_HZ   433920000U
#define CC1101_MAX_TEST_MS       60000U
#define CC1101_RSSI_DELAY_MS     2U
#define CC1101_TX_DRY_CODE       0x000A6764U
#define CC1101_TX_DRY_BITS       24U
#define CC1101_TX_DRY_REPEATS    10U
#define CC1101_TX_DRY_TE_US      350U
#define CC1101_TX_DRY_SYNC_LOW   (31U * CC1101_TX_DRY_TE_US)
#define CC1101_TX_DRY_MAX_PULSES ((CC1101_TX_DRY_BITS * 2U) + 2U)
#define CC1101_TX_TEST_CODE      0x005AA55AU
#define CC1101_TX_TEST_REPEATS   3U
#define CC1101_TX_TEST_PA_DBM    (-30)
#define CC1101_TX_TEST_PA_VALUE  0x12U
#define CC1101_TX_TEST_BAUD      (1000000.0f / (float)CC1101_TX_DRY_TE_US)
#define CC1101_TX_TEST_MDMCFG4   0x06U
#define CC1101_TX_TEST_MDMCFG3   0xCDU
#define CC1101_TX_TEST_WAIT_MS   750U
#define CC1101_TX_TEST_ARM_TOKEN "ARMED"
#define CC1101_TX_SQUARE_CYCLES  3U
#define CC1101_TX_SQUARE_HALF_US 10000U

#define LORA_DIAG_DEFAULT_HZ      906875000U
#define LORA_DIAG_DEFAULT_RSSI_MS 3000U
#define LORA_DIAG_DEFAULT_RX_MS   10000U
#define LORA_DIAG_MAX_TEST_MS     60000U
#define LORA_DIAG_SAMPLE_MS       20U
#define LORA_DIAG_TX_POWER_DBM    (-9)
#define LORA_DIAG_TX_ARM_TOKEN    "ARMED"

static sx1262_config_t s_lora_diag_cfg;
static bool s_lora_diag_pm_held = false;

static bool s_lora_diag_has_radio;

static void lora_diag_end(void) {
  if (!s_lora_diag_has_radio)
    return;
  if (sx1262_is_running()) {
    sx1262_stop_rx();
  }
  if (sx1262_deinit() != ESP_OK)
    return;
  if (s_lora_diag_pm_held) {
    if (power_manager_no_sleep_release() != ESP_OK)
      return;
    s_lora_diag_pm_held = false;
  }
  memset(&s_lora_diag_cfg, 0, sizeof(s_lora_diag_cfg));
  (void)lora_session_radio_release(&s_lora_diag_cfg);
  s_lora_diag_has_radio = false;
}

static esp_err_t lora_diag_begin(uint32_t frequency_hz) {
  if (lora_lab_active()) {
    printf("LoRa diagnostic refused: Radio Lab owns the radio\n");
    return ESP_ERR_INVALID_STATE;
  }
  if (lora_session_active() != LORA_PROTO_NONE) {
    printf("LoRa diagnostic refused: mesh session owns the radio; reboot "
           "before lab tests\n");
    return ESP_ERR_INVALID_STATE;
  }
  if (frequency_hz < 150000000U || frequency_hz > 960000000U) {
    return ESP_ERR_INVALID_ARG;
  }

  lora_diag_end();
  if (s_lora_diag_has_radio)
    return ESP_ERR_INVALID_STATE;
  esp_err_t err = lora_session_radio_reserve(&s_lora_diag_cfg);
  if (err != ESP_OK)
    return err;
  s_lora_diag_has_radio = true;
  err = power_manager_no_sleep_acquire();
  if (err != ESP_OK) {
    lora_diag_end();
    return err;
  }
  s_lora_diag_pm_held = true;

  memset(&s_lora_diag_cfg, 0, sizeof(s_lora_diag_cfg));
  err = sx1262_hal_create(&s_lora_diag_cfg.hal);
  if (err != ESP_OK) {
    lora_diag_end();
    return err;
  }

  s_lora_diag_cfg.frequency_hz = frequency_hz;
  s_lora_diag_cfg.sf = SX1262_LORA_SF7;
  s_lora_diag_cfg.bw = SX1262_LORA_BW_125;
  s_lora_diag_cfg.cr = SX1262_LORA_CR_4_5;
  s_lora_diag_cfg.tx_power_dbm = LORA_DIAG_TX_POWER_DBM;
  s_lora_diag_cfg.preamble_len = 12;
  s_lora_diag_cfg.is_crc_on = true;
  s_lora_diag_cfg.is_inverted_iq = false;
  s_lora_diag_cfg.is_implicit_hdr = false;
  s_lora_diag_cfg.is_public_network = false;

  err = sx1262_init(&s_lora_diag_cfg);
  if (err != ESP_OK) {
    lora_diag_end();
    return err;
  }

  sx1262_callbacks_t callbacks = {0};
  err = sx1262_set_callbacks(&callbacks);
  if (err == ESP_OK) {
    err = sx1262_start();
  }
  if (err != ESP_OK) {
    lora_diag_end();
  }
  return err;
}

static bool lora_parse_u32(const char *text, uint32_t *out) {
  if (text == NULL || out == NULL) {
    return false;
  }
  char *end = NULL;
  unsigned long value = strtoul(text, &end, 0);
  if (end == text || *end != '\0' || value > UINT32_MAX) {
    return false;
  }
  *out = (uint32_t)value;
  return true;
}

static int lora_diag_status(uint32_t frequency_hz) {
  esp_err_t err = lora_diag_begin(frequency_hz);
  if (err != ESP_OK) {
    printf("SX1262 bring-up: FAIL (%s)\n", esp_err_to_name(err));
    return 1;
  }

  uint8_t status = 0;
  uint16_t errors = 0;
  uint8_t packet_type = 0xFF;
  uint8_t irq_raw[2] = {0};
  uint8_t sync_word[2] = {0};
  esp_err_t status_err = sx1262_get_status(&status);
  esp_err_t errors_err = sx1262_get_device_errors(&errors);
  esp_err_t type_err =
      sx1262_cmd_read(&s_lora_diag_cfg.hal, SX1262_OP_GET_PACKET_TYPE, &packet_type, 1);
  esp_err_t irq_err =
      sx1262_cmd_read(&s_lora_diag_cfg.hal, SX1262_OP_GET_IRQ_STATUS, irq_raw, sizeof(irq_raw));
  esp_err_t sync_err = sx1262_cmd_read_register(
      &s_lora_diag_cfg.hal, SX1262_REG_LORA_SYNC_WORD_MSB, sync_word, sizeof(sync_word));

  uint8_t mode = (status & SX1262_STATUS_CHIP_MODE_MASK) >> SX1262_STATUS_CHIP_MODE_SHIFT;
  uint8_t command = (status & SX1262_STATUS_CMD_STATUS_MASK) >> SX1262_STATUS_CMD_STATUS_SHIFT;
  uint16_t irq = ((uint16_t)irq_raw[0] << 8) | irq_raw[1];
  bool pass = status_err == ESP_OK && errors_err == ESP_OK && type_err == ESP_OK &&
              irq_err == ESP_OK && sync_err == ESP_OK && status != 0x00 && status != 0xFF &&
              (mode == SX1262_CHIP_MODE_STDBY_RC || mode == SX1262_CHIP_MODE_STDBY_XOSC) &&
              packet_type == SX1262_PACKET_TYPE_LORA && errors == 0 &&
              sync_word[0] == (uint8_t)(SX1262_LORA_SYNC_WORD_PRIVATE >> 8) &&
              sync_word[1] == (uint8_t)SX1262_LORA_SYNC_WORD_PRIVATE;

  printf("SX1262 bring-up: %s\n", pass ? "PASS" : "FAIL");
  printf("  SPI status=0x%02X mode=%u command=%u packet_type=0x%02X\n",
         status,
         mode,
         command,
         packet_type);
  printf("  BUSY(GPIO%d)=%d DIO1(GPIO%d)=%d NSS(GPIO%d)=%d IRQ=0x%04X "
         "errors=0x%04X\n",
         GPIO_LORA_BUSY_PIN,
         gpio_get_level(GPIO_LORA_BUSY_PIN),
         GPIO_LORA_DIO1_PIN,
         gpio_get_level(GPIO_LORA_DIO1_PIN),
         GPIO_LORA_CS_PIN,
         gpio_get_level(GPIO_LORA_CS_PIN),
         irq,
         errors);
  printf("  freq=%lu Hz SF7 BW125 CR4/5 sync=%02X%02X TX-config=%d dBm "
         "RF-TX=OFF\n",
         (unsigned long)frequency_hz,
         sync_word[0],
         sync_word[1],
         LORA_DIAG_TX_POWER_DBM);

  lora_diag_end();
  return pass ? 0 : 1;
}

static int lora_diag_rssi(uint32_t frequency_hz, uint32_t duration_ms) {
  esp_err_t err = lora_diag_begin(frequency_hz);
  if (err != ESP_OK) {
    printf("SX1262 RSSI: bring-up FAIL (%s)\n", esp_err_to_name(err));
    return 1;
  }
  err = sx1262_receive_continuous();
  if (err != ESP_OK) {
    printf("SX1262 RSSI: RX start FAIL (%s)\n", esp_err_to_name(err));
    lora_diag_end();
    return 1;
  }

  int16_t weakest = INT16_MAX;
  int16_t strongest = INT16_MIN;
  int64_t sum = 0;
  uint32_t count = 0;
  uint32_t elapsed = 0;
  uint32_t next_report = 500;
  printf("SX1262 RSSI RX-only: START freq=%lu duration=%lu ms\n",
         (unsigned long)frequency_hz,
         (unsigned long)duration_ms);
  while (elapsed < duration_ms) {
    int16_t rssi = 0;
    err = sx1262_get_rssi_inst(&rssi);
    if (err != ESP_OK) {
      break;
    }
    if (rssi < weakest)
      weakest = rssi;
    if (rssi > strongest)
      strongest = rssi;
    sum += rssi;
    count++;
    if (elapsed >= next_report) {
      printf("  t=%lums RSSI=%d dBm strongest=%d dBm\n", (unsigned long)elapsed, rssi, strongest);
      next_report += 500;
    }
    vTaskDelay(pdMS_TO_TICKS(LORA_DIAG_SAMPLE_MS));
    elapsed += LORA_DIAG_SAMPLE_MS;
  }

  sx1262_stats_t stats = {0};
  esp_err_t stats_err = sx1262_get_stats(&stats);
  bool pass = err == ESP_OK && count > 0 && weakest <= 0 && strongest <= 0;
  printf("SX1262 RSSI: %s samples=%lu min/avg/max=%d/%lld/%d dBm",
         pass ? "PASS" : "FAIL",
         (unsigned long)count,
         count ? weakest : 0,
         count ? (long long)(sum / (int64_t)count) : 0,
         count ? strongest : 0);
  if (stats_err == ESP_OK) {
    printf(" packets=%u crc=%u header=%u",
           stats.nb_pkt_received,
           stats.nb_crc_error,
           stats.nb_header_error);
  }
  printf("\n");
  lora_diag_end();
  return pass ? 0 : 1;
}

static int lora_diag_rx(uint32_t frequency_hz, uint32_t duration_ms) {
  esp_err_t err = lora_diag_begin(frequency_hz);
  if (err != ESP_OK) {
    printf("SX1262 RX: bring-up FAIL (%s)\n", esp_err_to_name(err));
    return 1;
  }
  err = sx1262_receive_continuous();
  if (err != ESP_OK) {
    printf("SX1262 RX: start FAIL (%s)\n", esp_err_to_name(err));
    lora_diag_end();
    return 1;
  }

  uint32_t elapsed = 0;
  uint32_t packets = 0;
  printf("SX1262 RX: LISTEN freq=%lu SF7 BW125 CR4/5 duration=%lu ms\n",
         (unsigned long)frequency_hz,
         (unsigned long)duration_ms);
  while (elapsed < duration_ms) {
    sx1262_packet_t packet = {0};
    if (sx1262_get_packet(&packet) == ESP_OK) {
      packets++;
      printf("  RX#%lu len=%u RSSI=%d dBm SNR=%d dB data=",
             (unsigned long)packets,
             packet.len,
             packet.rssi_pkt_dbm,
             packet.snr_pkt_db);
      for (uint8_t i = 0; i < packet.len; i++) {
        printf("%02X", packet.buf[i]);
      }
      printf(" text='");
      for (uint8_t i = 0; i < packet.len; i++) {
        uint8_t ch = packet.buf[i];
        putchar((ch >= 32 && ch <= 126) ? (char)ch : '.');
      }
      printf("'\n");
      if (sx1262_get_state() != SX1262_STATE_RX) {
        (void)sx1262_receive_continuous();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    elapsed += 10;
  }

  printf("SX1262 RX: DONE packets=%lu\n", (unsigned long)packets);
  lora_diag_end();
  return 0;
}

static int lora_diag_tx(uint32_t frequency_hz, const char *text) {
  size_t len = strlen(text);
  if (len == 0 || len > 64) {
    printf("SX1262 TX: payload must be 1..64 bytes\n");
    return 1;
  }
  esp_err_t err = lora_diag_begin(frequency_hz);
  if (err != ESP_OK) {
    printf("SX1262 TX: bring-up FAIL (%s); RF=OFF\n", esp_err_to_name(err));
    return 1;
  }

  printf("SX1262 TX: START freq=%lu power=%d dBm len=%u\n",
         (unsigned long)frequency_hz,
         LORA_DIAG_TX_POWER_DBM,
         (unsigned)len);
  err = sx1262_transmit((const uint8_t *)text, (uint8_t)len, 3000);
  uint32_t waited_ms = 0;
  while (err == ESP_OK && sx1262_get_state() != SX1262_STATE_STDBY_RC && waited_ms < 4000) {
    vTaskDelay(pdMS_TO_TICKS(10));
    waited_ms += 10;
  }
  bool pass = err == ESP_OK && sx1262_get_state() == SX1262_STATE_STDBY_RC;
  printf(
      "SX1262 TX: %s elapsed<=%lu ms RF=OFF\n", pass ? "PASS" : "FAIL", (unsigned long)waited_ms);
  lora_diag_end();
  return pass ? 0 : 1;
}

typedef struct {
  int nss_high;
  int nss_low;
  int miso_high;
  int miso_low;
  uint8_t unselected_raw[2];
  uint8_t selected_raw[2];
  int unselected_spi;
  int selected_spi;
} lora_diag_probe_sample_t;

static int lora_diag_probe_once(sx1262_hal_t *hal,
                                gpio_pull_mode_t pull,
                                const char *name,
                                lora_diag_probe_sample_t *sample) {
  memset(sample, 0, sizeof(*sample));
  gpio_set_pull_mode(GPIO_LORA_MISO_PIN, pull);
  hal->cs_high(hal->ctx);
  vTaskDelay(pdMS_TO_TICKS(2));
  sample->nss_high = gpio_get_level(GPIO_LORA_CS_PIN);
  sample->miso_high = gpio_get_level(GPIO_LORA_MISO_PIN);
  int busy_idle = gpio_get_level(GPIO_LORA_BUSY_PIN);

  uint8_t idle_tx[2] = {0x00, 0x00};
  uint8_t status_tx[2] = {SX1262_OP_GET_STATUS, 0x00};
  hal->lock(hal->ctx);

  /* With NSS high, no device should drive MISO: raw bytes must follow the pull.
   */
  sample->unselected_spi =
      hal->spi_transfer(hal->ctx, idle_tx, sample->unselected_raw, sizeof(idle_tx));

  hal->cs_low(hal->ctx);
  hal->delay_ms(hal->ctx, 1);
  sample->nss_low = gpio_get_level(GPIO_LORA_CS_PIN);
  sample->miso_low = gpio_get_level(GPIO_LORA_MISO_PIN);
  sample->selected_spi =
      hal->spi_transfer(hal->ctx, status_tx, sample->selected_raw, sizeof(status_tx));
  hal->cs_high(hal->ctx);
  hal->unlock(hal->ctx);
  vTaskDelay(pdMS_TO_TICKS(2));

  printf("  %-9s NSS-H=%d MISO=%d raw(no-select)=%02X %02X spi=%d; "
         "NSS-L=%d MISO=%d raw(selected)=%02X %02X spi=%d BUSY=%d\n",
         name,
         sample->nss_high,
         sample->miso_high,
         sample->unselected_raw[0],
         sample->unselected_raw[1],
         sample->unselected_spi,
         sample->nss_low,
         sample->miso_low,
         sample->selected_raw[0],
         sample->selected_raw[1],
         sample->selected_spi,
         busy_idle);
  return sample->unselected_spi == 0 && sample->selected_spi == 0 ? 0 : 1;
}

static int lora_diag_probe(void) {
  if (lora_lab_active() || lora_session_active() != LORA_PROTO_NONE || sx1262_is_running()) {
    printf("SX1262 electrical probe refused: radio is in use\n");
    return 1;
  }

  esp_err_t err = power_manager_no_sleep_acquire();
  if (err != ESP_OK) {
    printf("SX1262 electrical probe: power lock failed (%s)\n", esp_err_to_name(err));
    return 1;
  }

  sx1262_hal_t hal = {0};
  err = sx1262_hal_create(&hal);
  if (err != ESP_OK) {
    (void)power_manager_no_sleep_release();
    printf("SX1262 electrical probe: HAL failed (%s)\n", esp_err_to_name(err));
    return 1;
  }

  /* Allow physical readback of the two GPIOs used to classify the result. */
  err = gpio_set_direction(GPIO_LORA_CS_PIN, GPIO_MODE_INPUT_OUTPUT);
  if (err == ESP_OK) {
    err = gpio_input_enable(GPIO_LORA_MISO_PIN);
  }
  if (err == ESP_OK) {
    err = gpio_input_enable(GPIO_CC1101_CS_PIN);
  }
  if (err == ESP_OK) {
    err = gpio_input_enable(GPIO_NFC_CS_PIN);
  }
  if (err == ESP_OK) {
    err = gpio_input_enable(GPIO_ST7789_CS_PIN);
  }
  if (err == ESP_OK) {
    err = gpio_input_enable(GPIO_QMI8658A_CS_PIN);
  }
  if (err != ESP_OK) {
    (void)sx1262_hal_destroy();
    (void)power_manager_no_sleep_release();
    printf("SX1262 electrical probe: GPIO readback setup failed (%s)\n", esp_err_to_name(err));
    return 1;
  }

  printf("SX1262 electrical probe (RF TX OFF): GPIO SCK=%d MOSI=%d MISO=%d "
         "NSS=%d BUSY=%d DIO1=%d\n",
         GPIO_LORA_SCLK_PIN,
         GPIO_LORA_MOSI_PIN,
         GPIO_LORA_MISO_PIN,
         GPIO_LORA_CS_PIN,
         GPIO_LORA_BUSY_PIN,
         GPIO_LORA_DIO1_PIN);
  printf("  other CS idle: CC1101(GPIO%d)=%d NFC(GPIO%d)=%d LCD(GPIO%d)=%d "
         "IMU(GPIO%d)=%d\n",
         GPIO_CC1101_CS_PIN,
         gpio_get_level(GPIO_CC1101_CS_PIN),
         GPIO_NFC_CS_PIN,
         gpio_get_level(GPIO_NFC_CS_PIN),
         GPIO_ST7789_CS_PIN,
         gpio_get_level(GPIO_ST7789_CS_PIN),
         GPIO_QMI8658A_CS_PIN,
         gpio_get_level(GPIO_QMI8658A_CS_PIN));
  lora_diag_probe_sample_t up = {0};
  lora_diag_probe_sample_t down = {0};
  int up_result = lora_diag_probe_once(&hal, GPIO_PULLUP_ONLY, "pull-up", &up);
  int down_result = lora_diag_probe_once(&hal, GPIO_PULLDOWN_ONLY, "pull-down", &down);
  gpio_set_pull_mode(GPIO_LORA_MISO_PIN, GPIO_FLOATING);

  bool nss_ok = up.nss_high == 1 && up.nss_low == 0 && down.nss_high == 1 && down.nss_low == 0;
  bool reference_follows_pull = up.unselected_raw[0] == 0xFF && up.unselected_raw[1] == 0xFF &&
                                down.unselected_raw[0] == 0x00 && down.unselected_raw[1] == 0x00;
  bool selected_follows_pull = up.selected_raw[0] == 0xFF && up.selected_raw[1] == 0xFF &&
                               down.selected_raw[0] == 0x00 && down.selected_raw[1] == 0x00;
  bool selected_stuck_low = up.selected_raw[0] == 0x00 && up.selected_raw[1] == 0x00 &&
                            down.selected_raw[0] == 0x00 && down.selected_raw[1] == 0x00;
  bool up_status_valid = up.selected_raw[1] != 0x00 && up.selected_raw[1] != 0xFF &&
                         (up.selected_raw[1] & SX1262_STATUS_CHIP_MODE_MASK) != 0;
  bool down_status_valid = down.selected_raw[1] != 0x00 && down.selected_raw[1] != 0xFF &&
                           (down.selected_raw[1] & SX1262_STATUS_CHIP_MODE_MASK) != 0;
  bool selected_has_response = up_status_valid && down_status_valid &&
                               (up.selected_raw[1] & SX1262_STATUS_CHIP_MODE_MASK) ==
                                   (down.selected_raw[1] & SX1262_STATUS_CHIP_MODE_MASK);

  if (!nss_ok) {
    printf("  RESULT: NSS GPIO readback FAIL (expected high=1 and low=0)\n");
  } else if (!reference_follows_pull) {
    printf("  RESULT: shared MISO held low while SX1262 NSS is high "
           "(pull-down/contention)\n");
  } else if (selected_has_response) {
    printf("  RESULT: SX1262 drives MISO with pull-independent status\n");
  } else if (selected_follows_pull) {
    printf("  RESULT: SX1262 does not drive MISO when selected "
           "(open/unpowered/NSS path)\n");
  } else if (selected_stuck_low) {
    printf("  RESULT: SX1262 branch actively holds MISO low when selected "
           "(reset/power/device fault)\n");
  } else {
    printf("  RESULT: selected response changes with pull; MISO drive is not "
           "confirmed\n");
  }
  printf("  final: NSS=%d MISO=%d BUSY=%d DIO1=%d\n",
         gpio_get_level(GPIO_LORA_CS_PIN),
         gpio_get_level(GPIO_LORA_MISO_PIN),
         gpio_get_level(GPIO_LORA_BUSY_PIN),
         gpio_get_level(GPIO_LORA_DIO1_PIN));

  (void)sx1262_hal_destroy();
  (void)power_manager_no_sleep_release();
  return (up_result == 0 && down_result == 0 && nss_ok && reference_follows_pull &&
          selected_has_response)
             ? 0
             : 1;
}

static int cmd_lora(int argc, char **argv) {
  if (argc < 2) {
    printf("usage: lora <probe|status|rssi|rx|tx> ...\n");
    printf("  probe                          electrical NSS/MISO/BUSY test; RF "
           "TX off\n");
    printf("  status [freq_hz]               SPI/BUSY/config self-test; RF TX "
           "off\n");
    printf("  rssi [freq_hz] [duration_ms]   RX-only noise/activity "
           "measurement\n");
    printf("  rx [freq_hz] [duration_ms]     receive raw LoRa SF7/BW125 "
           "packets\n");
    printf("  tx ARMED <freq_hz> [text]      one -9 dBm lab packet; legal band "
           "only\n");
    return 1;
  }

  if (strcmp(argv[1], "probe") == 0) {
    return lora_diag_probe();
  }

  uint32_t frequency_hz = LORA_DIAG_DEFAULT_HZ;
  if (strcmp(argv[1], "status") == 0) {
    if (argc >= 3 && !lora_parse_u32(argv[2], &frequency_hz)) {
      printf("lora: invalid frequency\n");
      return 1;
    }
    return lora_diag_status(frequency_hz);
  }

  if (strcmp(argv[1], "rssi") == 0 || strcmp(argv[1], "rx") == 0) {
    uint32_t duration_ms =
        strcmp(argv[1], "rssi") == 0 ? LORA_DIAG_DEFAULT_RSSI_MS : LORA_DIAG_DEFAULT_RX_MS;
    if (argc >= 3 && !lora_parse_u32(argv[2], &frequency_hz)) {
      printf("lora: invalid frequency\n");
      return 1;
    }
    if (argc >= 4 && !lora_parse_u32(argv[3], &duration_ms)) {
      printf("lora: invalid duration\n");
      return 1;
    }
    if (duration_ms == 0 || duration_ms > LORA_DIAG_MAX_TEST_MS) {
      printf("lora: duration must be 1..%u ms\n", LORA_DIAG_MAX_TEST_MS);
      return 1;
    }
    return strcmp(argv[1], "rssi") == 0 ? lora_diag_rssi(frequency_hz, duration_ms)
                                        : lora_diag_rx(frequency_hz, duration_ms);
  }

  if (strcmp(argv[1], "tx") == 0) {
    if (argc < 4 || strcmp(argv[2], LORA_DIAG_TX_ARM_TOKEN) != 0 ||
        !lora_parse_u32(argv[3], &frequency_hz)) {
      printf("SX1262 TX NOT ARMED; RF=OFF\n");
      printf("  usage: lora tx %s <freq_hz> [text]\n", LORA_DIAG_TX_ARM_TOKEN);
      return 1;
    }
    const char *text = argc >= 5 ? argv[4] : "HB-LORA-TEST";
    return lora_diag_tx(frequency_hz, text);
  }

  printf("lora: unknown subcommand '%s'\n", argv[1]);
  return 1;
}

static bool cc1101_validate_p1_frame(
    const int32_t *pulses, size_t count, uint32_t code, uint32_t *total_us, uint32_t *high_us) {
  if (pulses == NULL || count != CC1101_TX_DRY_MAX_PULSES)
    return false;

  uint32_t total = 0;
  uint32_t high = 0;
  uint32_t recovered = 0;
  for (uint32_t bit_idx = 0; bit_idx < CC1101_TX_DRY_BITS; bit_idx++) {
    int32_t mark = pulses[bit_idx * 2U];
    int32_t space = pulses[(bit_idx * 2U) + 1U];
    bool zero =
        mark == (int32_t)CC1101_TX_DRY_TE_US && space == -(int32_t)(3U * CC1101_TX_DRY_TE_US);
    bool one =
        mark == (int32_t)(3U * CC1101_TX_DRY_TE_US) && space == -(int32_t)CC1101_TX_DRY_TE_US;
    if (!zero && !one)
      return false;
    recovered = (recovered << 1) | (one ? 1U : 0U);
    high += (uint32_t)mark;
    total += (uint32_t)mark + (uint32_t)(-space);
  }

  int32_t sync_mark = pulses[CC1101_TX_DRY_BITS * 2U];
  int32_t sync_space = pulses[(CC1101_TX_DRY_BITS * 2U) + 1U];
  if (sync_mark != (int32_t)CC1101_TX_DRY_TE_US || sync_space != -(int32_t)CC1101_TX_DRY_SYNC_LOW ||
      recovered != code) {
    return false;
  }
  high += (uint32_t)sync_mark;
  total += (uint32_t)sync_mark + (uint32_t)(-sync_space);
  if (total_us != NULL)
    *total_us = total;
  if (high_us != NULL)
    *high_us = high;
  return true;
}

static uint8_t cc1101_status_reg(uint8_t reg) {
  return cc1101_read_reg(reg | 0x40U);
}

static bool cc1101_freq_supported(uint32_t hz) {
  return (hz >= 300000000U && hz <= 348000000U) || (hz >= 387000000U && hz <= 464000000U) ||
         (hz >= 779000000U && hz <= 928000000U);
}

static uint32_t cc1101_frequency_from_regs(void) {
  uint32_t word = ((uint32_t)cc1101_read_reg(CC1101_FREQ2) << 16) |
                  ((uint32_t)cc1101_read_reg(CC1101_FREQ1) << 8) |
                  (uint32_t)cc1101_read_reg(CC1101_FREQ0);
  return (uint32_t)(((uint64_t)word * 26000000ULL) / 65536ULL);
}

static int cmd_cc1101(int argc, char **argv) {
  if (argc < 2) {
    printf("usage: cc1101 "
           "<status|watch|watchgain|survey|capture|cscapture|pacheck|txdry|"
           "txtest|txgpio|txsquare|stop>\n");
    printf("  watch [freq_hz] [duration_ms]   measure RSSI while a key is "
           "pressed\n");
    printf("  watchgain [freq_hz] [duration_ms] full-gain OOK receiver "
           "diagnostic\n");
    printf("  survey [duration_ms]            compare 315/433/868/915 MHz "
           "bands\n");
    printf("  capture [freq_hz] [duration_ms] capture/decode OOK pulses (RX "
           "only)\n");
    printf("  cscapture [freq_hz] [duration_ms] capture GDO0 carrier-sense "
           "edges\n");
    printf("  pacheck                        verify OOK PATABLE write/read; TX "
           "RF stays OFF\n");
    printf("  txdry                         validate captured P1 waveform; RF "
           "stays OFF\n");
    printf("  txtest ARMED                  -30 dBm, 3-frame lab code; "
           "controlled test only\n");
    printf("  txgpio ARMED                 same lab code using direct GPIO "
           "timing\n");
    printf("  txsquare ARMED                -30 dBm, slow OOK modulation "
           "diagnostic\n");
    return 1;
  }

  if (strcmp(argv[1], "status") == 0) {
    if (!cc1101_is_initialized())
      cc1101_init();
    uint8_t part = cc1101_status_reg(CC1101_PARTNUM);
    uint8_t version = cc1101_status_reg(CC1101_VERSION);
    bool recovery_attempted = false;
    if (part != 0x00U || version == 0x00U || version == 0xFFU) {
      recovery_attempted = true;
      if (cc1101_recover() == ESP_OK) {
        part = cc1101_status_reg(CC1101_PARTNUM);
        version = cc1101_status_reg(CC1101_VERSION);
      }
    }
    uint8_t marc = cc1101_status_reg(CC1101_MARCSTATE) & 0x1FU;
    uint8_t pkt = cc1101_status_reg(CC1101_PKTSTATUS);
    uint8_t raw_rssi = cc1101_status_reg(CC1101_RSSI);
    printf("CC1101: PARTNUM=0x%02X VERSION=0x%02X %s\n",
           part,
           version,
           (part == 0x00U && version != 0x00U && version != 0xFFU) ? "SPI=PASS" : "SPI=FAIL");
    if (recovery_attempted)
      printf("  automatic recovery: %s\n",
             (part == 0x00U && version != 0x00U && version != 0xFFU) ? "PASS" : "FAIL");
    printf("  state=0x%02X freq=%lu Hz RSSI=%.1f dBm PKTSTATUS=0x%02X GDO0=%d "
           "GDO2=%d\n",
           marc,
           (unsigned long)cc1101_frequency_from_regs(),
           (double)cc1101_convert_rssi(raw_rssi),
           pkt,
           gpio_get_level(GPIO_CC1101_GDO0_PIN),
           gpio_get_level(GPIO_CC1101_GDO2_PIN));
    printf("  PE613050A: V1(GPIO17)=%d V2(GPIO18)=%d (RF1 tuning branch "
           "selected)\n",
           gpio_get_level(GPIO_RF_SW_V1_PIN),
           gpio_get_level(GPIO_RF_SW_V2_PIN));
    printf("  regs: IOCFG2=%02X IOCFG0=%02X PKTCTRL0=%02X MDMCFG4=%02X "
           "MDMCFG3=%02X "
           "MDMCFG2=%02X AGC=%02X/%02X/%02X\n",
           cc1101_read_reg(CC1101_IOCFG2),
           cc1101_read_reg(CC1101_IOCFG0),
           cc1101_read_reg(CC1101_PKTCTRL0),
           cc1101_read_reg(CC1101_MDMCFG4),
           cc1101_read_reg(CC1101_MDMCFG3),
           cc1101_read_reg(CC1101_MDMCFG2),
           cc1101_read_reg(CC1101_AGCCTRL2),
           cc1101_read_reg(CC1101_AGCCTRL1),
           cc1101_read_reg(CC1101_AGCCTRL0));
    uint8_t pa_table[8] = {0};
    cc1101_read_burst(CC1101_PATABLE, pa_table, sizeof(pa_table));
    printf("  PATABLE: %02X %02X %02X %02X %02X %02X %02X %02X\n",
           pa_table[0],
           pa_table[1],
           pa_table[2],
           pa_table[3],
           pa_table[4],
           pa_table[5],
           pa_table[6],
           pa_table[7]);
    return (part == 0x00U && version != 0x00U && version != 0xFFU) ? 0 : 1;
  }

  if (strcmp(argv[1], "stop") == 0) {
    subghz_receiver_stop();
    vTaskDelay(pdMS_TO_TICKS(25));
    cc1101_strobe(CC1101_SIDLE);
    printf("CC1101 receiver stopped; radio IDLE\n");
    return 0;
  }

  if (strcmp(argv[1], "pacheck") == 0) {
    subghz_receiver_stop();
    vTaskDelay(pdMS_TO_TICKS(25));
    if (!cc1101_is_initialized())
      cc1101_init();

    uint8_t part_before = cc1101_status_reg(CC1101_PARTNUM);
    uint8_t version_before = cc1101_status_reg(CC1101_VERSION);
    if (part_before != 0x00U || version_before != 0x14U) {
      printf("CC1101 PA check: ABORT; initial SPI identity=%02X/%02X; RF=OFF\n",
             part_before,
             version_before);
      return 1;
    }

    /* This preset briefly enters RX while it is configured; SIDLE is issued
     * before inspection and STX is never called by this diagnostic. */
    cc1101_enable_async_mode(CC1101_DEFAULT_TEST_HZ);
    cc1101_strobe(CC1101_SIDLE);
    vTaskDelay(pdMS_TO_TICKS(3));

    uint8_t async_table[8] = {0};
    cc1101_read_burst(CC1101_PATABLE, async_table, sizeof(async_table));
    uint8_t async_part = cc1101_status_reg(CC1101_PARTNUM);
    uint8_t async_version = cc1101_status_reg(CC1101_VERSION);
    uint8_t async_frend0 = cc1101_read_reg(CC1101_FREND0);
    uint8_t async_mdmcfg2 = cc1101_read_reg(CC1101_MDMCFG2);
    uint8_t async_pktctrl0 = cc1101_read_reg(CC1101_PKTCTRL0);
    bool async_ok = async_part == 0x00U && async_version == 0x14U && async_frend0 == 0x11U &&
                    async_table[0] == 0x00U && async_table[1] == 0xC0U && async_mdmcfg2 == 0x30U &&
                    async_pktctrl0 == 0x32U;
    printf("CC1101 PA check stage ASYNC: id=%02X/%02X FREND0=%02X MDMCFG2=%02X "
           "PKTCTRL0=%02X PATABLE=%02X/%02X %s\n",
           async_part,
           async_version,
           async_frend0,
           async_mdmcfg2,
           async_pktctrl0,
           async_table[0],
           async_table[1],
           async_ok ? "PASS" : "FAIL");

    cc1101_set_pa(CC1101_TX_TEST_PA_DBM);
    vTaskDelay(pdMS_TO_TICKS(3));
    uint8_t low_table[8] = {0};
    cc1101_read_burst(CC1101_PATABLE, low_table, sizeof(low_table));
    uint8_t low_part = cc1101_status_reg(CC1101_PARTNUM);
    uint8_t low_version = cc1101_status_reg(CC1101_VERSION);
    uint8_t low_frend0 = cc1101_read_reg(CC1101_FREND0);
    bool low_ok = low_part == 0x00U && low_version == 0x14U && low_frend0 == 0x11U &&
                  low_table[0] == 0x00U && low_table[1] == 0x12U;
    printf("CC1101 PA check stage -30dBm: id=%02X/%02X FREND0=%02X "
           "PATABLE=%02X/%02X %s\n",
           low_part,
           low_version,
           low_frend0,
           low_table[0],
           low_table[1],
           low_ok ? "PASS" : "FAIL");

    esp_err_t restore_err = cc1101_recover();
    uint8_t restored_version = cc1101_status_reg(CC1101_VERSION);
    bool restored = restore_err == ESP_OK && restored_version == 0x14U;
    printf("CC1101 PA check: %s; STX not called; RF TX=OFF; restore=%s\n",
           (async_ok && low_ok && restored) ? "PASS" : "FAIL",
           restored ? "PASS" : "FAIL");
    return (async_ok && low_ok && restored) ? 0 : 1;
  }

  if (strcmp(argv[1], "txdry") == 0) {
    const subghz_protocol_t *protocol = subghz_protocol_registry_get_by_name("RCSwitch");
    if (protocol == NULL || protocol->encode == NULL) {
      printf("CC1101 TX dry-run: FAIL (RCSwitch encoder unavailable); RF=OFF\n");
      return 1;
    }

    subghz_data_t data = {
        .protocol_name = "RCSwitch(P1)",
        .serial = CC1101_TX_DRY_CODE,
        .btn = 0,
        .bit_count = CC1101_TX_DRY_BITS,
        .raw_value = CC1101_TX_DRY_CODE,
    };
    int32_t pulses[CC1101_TX_DRY_MAX_PULSES] = {0};
    size_t count = protocol->encode(&data, pulses, CC1101_TX_DRY_MAX_PULSES);
    uint32_t frame_us = 0;
    uint32_t high_us = 0;
    bool valid = cc1101_validate_p1_frame(pulses, count, CC1101_TX_DRY_CODE, &frame_us, &high_us);

    printf("CC1101 TX dry-run: %s; RF=OFF (STX was not called)\n", valid ? "PASS" : "FAIL");
    printf("  freq=%lu Hz protocol=RCSwitch(P1) code=0x%06lX bits=%u TE=%u us\n",
           (unsigned long)CC1101_DEFAULT_TEST_HZ,
           (unsigned long)CC1101_TX_DRY_CODE,
           CC1101_TX_DRY_BITS,
           CC1101_TX_DRY_TE_US);
    printf("  frame=data+sync pulses=%u duration=%lu us high=%lu us duty=%.1f%%\n",
           (unsigned)count,
           (unsigned long)frame_us,
           (unsigned long)high_us,
           frame_us ? (100.0 * (double)high_us / (double)frame_us) : 0.0);
    printf("  planned repeats=%u burst=%lu us planned initial PA=-30 dBm\n",
           CC1101_TX_DRY_REPEATS,
           (unsigned long)(frame_us * CC1101_TX_DRY_REPEATS));
    return valid ? 0 : 1;
  }

  if (strcmp(argv[1], "txgpio") == 0) {
    if (argc != 3 || strcmp(argv[2], CC1101_TX_TEST_ARM_TOKEN) != 0) {
      printf("CC1101 TX GPIO test NOT ARMED; RF=OFF\n");
      printf("  controlled two-board test only: cc1101 txgpio %s\n", CC1101_TX_TEST_ARM_TOKEN);
      return 1;
    }

    const subghz_protocol_t *protocol = subghz_protocol_registry_get_by_name("RCSwitch");
    if (protocol == NULL || protocol->encode == NULL) {
      printf("CC1101 TX GPIO test: FAIL (RCSwitch encoder unavailable); RF=OFF\n");
      return 1;
    }

    subghz_data_t data = {
        .protocol_name = "RCSwitch(P1)",
        .serial = CC1101_TX_TEST_CODE,
        .btn = 0,
        .bit_count = CC1101_TX_DRY_BITS,
        .raw_value = CC1101_TX_TEST_CODE,
    };
    int32_t frame[CC1101_TX_DRY_MAX_PULSES] = {0};
    size_t frame_count = protocol->encode(&data, frame, CC1101_TX_DRY_MAX_PULSES);
    uint32_t frame_us = 0;
    uint32_t high_us = 0;
    if (!cc1101_validate_p1_frame(frame, frame_count, CC1101_TX_TEST_CODE, &frame_us, &high_us)) {
      printf("CC1101 TX GPIO test: FAIL (waveform validation); RF=OFF\n");
      return 1;
    }

    int32_t burst[CC1101_TX_DRY_MAX_PULSES * CC1101_TX_TEST_REPEATS];
    for (uint32_t repeat = 0; repeat < CC1101_TX_TEST_REPEATS; repeat++) {
      memcpy(&burst[repeat * frame_count], frame, frame_count * sizeof(frame[0]));
    }
    const size_t burst_count = frame_count * CC1101_TX_TEST_REPEATS;

    subghz_receiver_stop();
    subghz_tx_stop();
    vTaskDelay(pdMS_TO_TICKS(25));
    gpio_set_direction(GPIO_CC1101_GDO0_PIN, GPIO_MODE_INPUT);
    cc1101_enable_async_mode(CC1101_DEFAULT_TEST_HZ);
    cc1101_strobe(CC1101_SIDLE);
    vTaskDelay(pdMS_TO_TICKS(3));
    /* Keep the documented asynchronous-data selection while the MCU remains
     * an input.  In TX the CC1101 automatically turns GDO0 into its data
     * input; only after MARCSTATE=TX may the MCU safely enable its driver. */
    cc1101_write_reg(CC1101_IOCFG0, 0x0DU);
    cc1101_set_frequency(CC1101_DEFAULT_TEST_HZ);
    cc1101_set_data_rate(CC1101_TX_TEST_BAUD);
    cc1101_set_pa(CC1101_TX_TEST_PA_DBM);

    uint8_t pa_table[8] = {0};
    cc1101_read_burst(CC1101_PATABLE, pa_table, sizeof(pa_table));
    uint8_t part = cc1101_status_reg(CC1101_PARTNUM);
    uint8_t version = cc1101_status_reg(CC1101_VERSION);
    uint8_t frend0 = cc1101_read_reg(CC1101_FREND0);
    uint8_t iocfg0 = cc1101_read_reg(CC1101_IOCFG0);
    uint8_t mdmcfg4 = cc1101_read_reg(CC1101_MDMCFG4);
    uint8_t mdmcfg3 = cc1101_read_reg(CC1101_MDMCFG3);
    bool config_ok = part == 0x00U && version == 0x14U && frend0 == 0x11U && iocfg0 == 0x0DU &&
                     pa_table[0] == 0x00U && pa_table[1] == CC1101_TX_TEST_PA_VALUE &&
                     mdmcfg4 == CC1101_TX_TEST_MDMCFG4 && mdmcfg3 == CC1101_TX_TEST_MDMCFG3;
    if (!config_ok) {
      printf("CC1101 TX GPIO test: ABORT config id=%02X/%02X FREND0=%02X "
             "IOCFG0=%02X PATABLE=%02X/%02X RATE=%02X/%02X; RF=OFF\n",
             part,
             version,
             frend0,
             iocfg0,
             pa_table[0],
             pa_table[1],
             mdmcfg4,
             mdmcfg3);
      cc1101_strobe(CC1101_SIDLE);
      return 1;
    }

    cc1101_enter_tx_mode();
    uint8_t marcstate = 0;
    for (uint32_t waited_ms = 0; waited_ms < 10U; waited_ms++) {
      marcstate = cc1101_status_reg(CC1101_MARCSTATE) & 0x1FU;
      if (marcstate == 0x13U)
        break;
      vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (marcstate != 0x13U) {
      gpio_set_direction(GPIO_CC1101_GDO0_PIN, GPIO_MODE_INPUT);
      cc1101_strobe(CC1101_SIDLE);
      printf("CC1101 TX GPIO test: FAIL state=0x%02X; RF=OFF\n", marcstate);
      return 1;
    }

    gpio_set_level(GPIO_CC1101_GDO0_PIN, 0);
    gpio_set_direction(GPIO_CC1101_GDO0_PIN, GPIO_MODE_INPUT_OUTPUT);

    printf("CC1101 TX GPIO test: START freq=%lu code=0x%06lX PA=%d dBm "
           "baud=%.1f rate=%02X/%02X frames=%u burst=%lu us\n",
           (unsigned long)CC1101_DEFAULT_TEST_HZ,
           (unsigned long)CC1101_TX_TEST_CODE,
           CC1101_TX_TEST_PA_DBM,
           (double)CC1101_TX_TEST_BAUD,
           mdmcfg4,
           mdmcfg3,
           CC1101_TX_TEST_REPEATS,
           (unsigned long)(frame_us * CC1101_TX_TEST_REPEATS));

    uint32_t pad_mismatches = 0;
    int64_t started_us = esp_timer_get_time();
    for (size_t i = 0; i < burst_count; i++) {
      int level = burst[i] > 0 ? 1 : 0;
      uint32_t duration = burst[i] > 0 ? (uint32_t)burst[i] : (uint32_t)(-burst[i]);
      gpio_set_level(GPIO_CC1101_GDO0_PIN, level);
      if (gpio_get_level(GPIO_CC1101_GDO0_PIN) != level)
        pad_mismatches++;
      esp_rom_delay_us(duration);
    }
    gpio_set_level(GPIO_CC1101_GDO0_PIN, 0);
    int64_t elapsed_us = esp_timer_get_time() - started_us;
    cc1101_strobe(CC1101_SIDLE);
    gpio_set_direction(GPIO_CC1101_GDO0_PIN, GPIO_MODE_INPUT);
    vTaskDelay(pdMS_TO_TICKS(1));
    marcstate = cc1101_status_reg(CC1101_MARCSTATE) & 0x1FU;
    bool pass = pad_mismatches == 0U && marcstate == 0x01U;
    printf("CC1101 TX GPIO test: %s elapsed=%lld us pad_mismatch=%lu "
           "state=0x%02X\n",
           pass ? "PASS" : "FAIL",
           (long long)elapsed_us,
           (unsigned long)pad_mismatches,
           marcstate);
    return pass ? 0 : 1;
  }

  if (strcmp(argv[1], "txtest") == 0) {
    if (argc != 3 || strcmp(argv[2], CC1101_TX_TEST_ARM_TOKEN) != 0) {
      printf("CC1101 TX test NOT ARMED; RF=OFF\n");
      printf("  controlled two-board test only: cc1101 txtest %s\n", CC1101_TX_TEST_ARM_TOKEN);
      return 1;
    }

    const subghz_protocol_t *protocol = subghz_protocol_registry_get_by_name("RCSwitch");
    if (protocol == NULL || protocol->encode == NULL) {
      printf("CC1101 TX test: FAIL (RCSwitch encoder unavailable); RF=OFF\n");
      return 1;
    }

    subghz_data_t data = {
        .protocol_name = "RCSwitch(P1)",
        .serial = CC1101_TX_TEST_CODE,
        .btn = 0,
        .bit_count = CC1101_TX_DRY_BITS,
        .raw_value = CC1101_TX_TEST_CODE,
    };
    int32_t frame[CC1101_TX_DRY_MAX_PULSES] = {0};
    size_t frame_count = protocol->encode(&data, frame, CC1101_TX_DRY_MAX_PULSES);
    uint32_t frame_us = 0;
    uint32_t high_us = 0;
    if (!cc1101_validate_p1_frame(frame, frame_count, CC1101_TX_TEST_CODE, &frame_us, &high_us)) {
      printf("CC1101 TX test: FAIL (waveform validation); RF=OFF\n");
      return 1;
    }

    int32_t burst[CC1101_TX_DRY_MAX_PULSES * CC1101_TX_TEST_REPEATS];
    for (uint32_t repeat = 0; repeat < CC1101_TX_TEST_REPEATS; repeat++) {
      memcpy(&burst[repeat * frame_count], frame, frame_count * sizeof(frame[0]));
    }

    subghz_receiver_stop();
    vTaskDelay(pdMS_TO_TICKS(25));
    esp_err_t err = subghz_tx_init();
    if (err != ESP_OK) {
      printf("CC1101 TX test: init FAIL: %s; RF=OFF\n", esp_err_to_name(err));
      return 1;
    }
    cc1101_set_frequency(CC1101_DEFAULT_TEST_HZ);
    /* Protocol 1 uses TE as its fundamental symbol period.  Match the
     * asynchronous modulator rate so TE=350 us is exactly one data bit. */
    cc1101_set_data_rate(CC1101_TX_TEST_BAUD);
    cc1101_set_pa(CC1101_TX_TEST_PA_DBM);

    uint8_t pa_table[8] = {0};
    cc1101_read_burst(CC1101_PATABLE, pa_table, sizeof(pa_table));
    uint8_t tx_part = cc1101_status_reg(CC1101_PARTNUM);
    uint8_t tx_version = cc1101_status_reg(CC1101_VERSION);
    uint8_t tx_frend0 = cc1101_read_reg(CC1101_FREND0);
    uint8_t tx_mdmcfg4 = cc1101_read_reg(CC1101_MDMCFG4);
    uint8_t tx_mdmcfg3 = cc1101_read_reg(CC1101_MDMCFG3);
    bool tx_config_ok = tx_part == 0x00U && tx_version == 0x14U && tx_frend0 == 0x11U &&
                        pa_table[0] == 0x00U && pa_table[1] == CC1101_TX_TEST_PA_VALUE &&
                        tx_mdmcfg4 == CC1101_TX_TEST_MDMCFG4 &&
                        tx_mdmcfg3 == CC1101_TX_TEST_MDMCFG3;
    if (!tx_config_ok) {
      printf("CC1101 TX test: ABORT config id=%02X/%02X FREND0=%02X "
             "PATABLE=%02X/%02X RATE=%02X/%02X; RF=OFF\n",
             tx_part,
             tx_version,
             tx_frend0,
             pa_table[0],
             pa_table[1],
             tx_mdmcfg4,
             tx_mdmcfg3);
      subghz_tx_stop();
      cc1101_recover();
      return 1;
    }

    printf("CC1101 TX test: START freq=%lu code=0x%06lX PA=%d dBm baud=%.1f "
           "rate=%02X/%02X frames=%u burst=%lu us\n",
           (unsigned long)CC1101_DEFAULT_TEST_HZ,
           (unsigned long)CC1101_TX_TEST_CODE,
           CC1101_TX_TEST_PA_DBM,
           (double)CC1101_TX_TEST_BAUD,
           tx_mdmcfg4,
           tx_mdmcfg3,
           CC1101_TX_TEST_REPEATS,
           (unsigned long)(frame_us * CC1101_TX_TEST_REPEATS));
    err = subghz_tx_send_raw(burst, CC1101_TX_DRY_MAX_PULSES * CC1101_TX_TEST_REPEATS);
    if (err == ESP_OK)
      vTaskDelay(pdMS_TO_TICKS(CC1101_TX_TEST_WAIT_MS));
    subghz_tx_stop();
    vTaskDelay(pdMS_TO_TICKS(25));
    cc1101_strobe(CC1101_SIDLE);
    uint8_t marc = cc1101_status_reg(CC1101_MARCSTATE) & 0x1FU;
    printf("CC1101 TX test: %s; radio state=0x%02X (%s)\n",
           (err == ESP_OK && marc == 0x01U) ? "QUEUED/PASS" : "FAIL",
           marc,
           marc == 0x01U ? "IDLE" : "NOT IDLE");
    return (err == ESP_OK && marc == 0x01U) ? 0 : 1;
  }

  if (strcmp(argv[1], "txsquare") == 0) {
    if (argc != 3 || strcmp(argv[2], CC1101_TX_TEST_ARM_TOKEN) != 0) {
      printf("CC1101 TX square test NOT ARMED; RF=OFF\n");
      printf("  controlled two-board test only: cc1101 txsquare %s\n", CC1101_TX_TEST_ARM_TOKEN);
      return 1;
    }

    int32_t burst[CC1101_TX_SQUARE_CYCLES * 2U];
    for (uint32_t cycle = 0; cycle < CC1101_TX_SQUARE_CYCLES; cycle++) {
      burst[cycle * 2U] = (int32_t)CC1101_TX_SQUARE_HALF_US;
      burst[(cycle * 2U) + 1U] = -(int32_t)CC1101_TX_SQUARE_HALF_US;
    }

    subghz_receiver_stop();
    vTaskDelay(pdMS_TO_TICKS(25));
    esp_err_t err = subghz_tx_init();
    if (err != ESP_OK) {
      printf("CC1101 TX square test: init FAIL: %s; RF=OFF\n", esp_err_to_name(err));
      return 1;
    }
    cc1101_set_frequency(CC1101_DEFAULT_TEST_HZ);
    cc1101_set_pa(CC1101_TX_TEST_PA_DBM);

    uint8_t pa_table[8] = {0};
    cc1101_read_burst(CC1101_PATABLE, pa_table, sizeof(pa_table));
    uint8_t tx_part = cc1101_status_reg(CC1101_PARTNUM);
    uint8_t tx_version = cc1101_status_reg(CC1101_VERSION);
    uint8_t tx_frend0 = cc1101_read_reg(CC1101_FREND0);
    bool tx_config_ok = tx_part == 0x00U && tx_version == 0x14U && tx_frend0 == 0x11U &&
                        pa_table[0] == 0x00U && pa_table[1] == CC1101_TX_TEST_PA_VALUE;
    if (!tx_config_ok) {
      printf("CC1101 TX square test: ABORT config id=%02X/%02X FREND0=%02X "
             "PATABLE=%02X/%02X; RF=OFF\n",
             tx_part,
             tx_version,
             tx_frend0,
             pa_table[0],
             pa_table[1]);
      subghz_tx_stop();
      cc1101_recover();
      return 1;
    }
    printf("CC1101 TX square test: START freq=%lu PA=%d dBm cycles=%u half=%u "
           "us total=%lu us\n",
           (unsigned long)CC1101_DEFAULT_TEST_HZ,
           CC1101_TX_TEST_PA_DBM,
           CC1101_TX_SQUARE_CYCLES,
           CC1101_TX_SQUARE_HALF_US,
           (unsigned long)(CC1101_TX_SQUARE_CYCLES * 2U * CC1101_TX_SQUARE_HALF_US));
    printf("  PATABLE[0..1]=%02X/%02X FREND0=%02X (expect OFF/nonzero and "
           "PA_POWER=1)\n",
           pa_table[0],
           pa_table[1],
           tx_frend0);

    err = subghz_tx_send_raw(burst, CC1101_TX_SQUARE_CYCLES * 2U);
    if (err == ESP_OK)
      vTaskDelay(pdMS_TO_TICKS(CC1101_TX_TEST_WAIT_MS));
    subghz_tx_stop();
    vTaskDelay(pdMS_TO_TICKS(25));
    cc1101_strobe(CC1101_SIDLE);
    uint8_t marc = cc1101_status_reg(CC1101_MARCSTATE) & 0x1FU;
    printf("CC1101 TX square test: %s; radio state=0x%02X (%s)\n",
           (err == ESP_OK && marc == 0x01U) ? "QUEUED/PASS" : "FAIL",
           marc,
           marc == 0x01U ? "IDLE" : "NOT IDLE");
    return (err == ESP_OK && marc == 0x01U) ? 0 : 1;
  }

  if (strcmp(argv[1], "watch") == 0 || strcmp(argv[1], "watchgain") == 0) {
    bool full_gain = strcmp(argv[1], "watchgain") == 0;
    uint32_t freq = argc >= 3 ? (uint32_t)strtoul(argv[2], NULL, 0) : CC1101_DEFAULT_TEST_HZ;
    uint32_t duration = argc >= 4 ? (uint32_t)strtoul(argv[3], NULL, 0) : 10000U;
    if (!cc1101_freq_supported(freq) || duration < 500U || duration > CC1101_MAX_TEST_MS) {
      printf("cc1101: unsupported frequency or duration (500..60000 ms)\n");
      return 1;
    }

    subghz_receiver_stop();
    vTaskDelay(pdMS_TO_TICKS(25));
    cc1101_set_preset(CC1101_PRESET_OOK_270KHZ, freq);
    if (full_gain) {
      /* Preserve MAGN_TARGET=7 while enabling all DVGA stages.  The normal
       * profile's 0xC7 blocks the three highest stages and puts a weak lab
       * signal near the OOK decision threshold. */
      cc1101_strobe(CC1101_SIDLE);
      cc1101_write_reg(CC1101_AGCCTRL2, 0x07U);
      cc1101_strobe(CC1101_SRX);
    }
    cc1101_write_reg(CC1101_IOCFG2,
                     0x0EU); // Carrier sense on GDO2/GPIO9 during diagnostics
    gpio_set_direction(GPIO_CC1101_GDO0_PIN, GPIO_MODE_INPUT);
    gpio_set_direction(GPIO_CC1101_GDO2_PIN, GPIO_MODE_INPUT);
    /* Let calibration/RSSI/AGC settle before establishing the noise floor. */
    vTaskDelay(pdMS_TO_TICKS(200));

    uint64_t start_us = esp_timer_get_time();
    uint64_t baseline_end_us = start_us + 250000ULL;
    uint64_t end_us = start_us + ((uint64_t)duration * 1000ULL);
    float baseline_sum = 0.0f;
    uint32_t baseline_count = 0;
    float minimum = 20.0f;
    float maximum = -140.0f;
    double sum = 0.0;
    uint32_t samples = 0;
    uint32_t gdo0_high = 0;
    uint32_t gdo2_high = 0;
    uint32_t pkt_cs_high = 0;
    uint32_t gdo0_edges = 0;
    uint32_t gdo2_edges = 0;
    uint32_t qualified_samples = 0;
    uint32_t qualified_run = 0;
    uint32_t longest_qualified_run = 0;
    bool baseline_ready = false;
    float baseline = -140.0f;
    int last_gdo0 = gpio_get_level(GPIO_CC1101_GDO0_PIN);
    int last_gdo2 = gpio_get_level(GPIO_CC1101_GDO2_PIN);

    printf("CC1101 RX watch%s: %lu Hz, %lu ms, AGCCTRL2=%02X; press the key now\n",
           full_gain ? " full-gain" : "",
           (unsigned long)freq,
           (unsigned long)duration,
           cc1101_read_reg(CC1101_AGCCTRL2));
    while ((uint64_t)esp_timer_get_time() < end_us) {
      float rssi = cc1101_convert_rssi(cc1101_status_reg(CC1101_RSSI));
      int gdo0 = gpio_get_level(GPIO_CC1101_GDO0_PIN);
      int gdo2 = gpio_get_level(GPIO_CC1101_GDO2_PIN);
      if (gdo0)
        gdo0_high++;
      if (gdo2)
        gdo2_high++;
      if ((cc1101_status_reg(CC1101_PKTSTATUS) & 0x40U) != 0U)
        pkt_cs_high++;
      if (gdo0 != last_gdo0)
        gdo0_edges++;
      if (gdo2 != last_gdo2)
        gdo2_edges++;
      last_gdo0 = gdo0;
      last_gdo2 = gdo2;
      if ((uint64_t)esp_timer_get_time() < baseline_end_us) {
        baseline_sum += rssi;
        baseline_count++;
      } else {
        if (!baseline_ready) {
          baseline = baseline_count ? baseline_sum / (float)baseline_count : rssi;
          baseline_ready = true;
        }
        if (rssi >= baseline + 8.0f) {
          qualified_samples++;
          qualified_run++;
          if (qualified_run > longest_qualified_run)
            longest_qualified_run = qualified_run;
        } else {
          qualified_run = 0;
        }
      }
      if (rssi < minimum)
        minimum = rssi;
      if (rssi > maximum)
        maximum = rssi;
      sum += rssi;
      samples++;
      vTaskDelay(pdMS_TO_TICKS(CC1101_RSSI_DELAY_MS));
    }
    cc1101_strobe(CC1101_SIDLE);
    cc1101_write_reg(CC1101_IOCFG2, 0x2EU);

    if (!baseline_ready)
      baseline = baseline_count ? baseline_sum / (float)baseline_count : minimum;
    float delta = maximum - baseline;
    printf("RSSI result: baseline=%.1f dBm min/avg/max=%.1f/%.1f/%.1f dBm "
           "delta=%.1f dB samples=%lu\n",
           (double)baseline,
           (double)minimum,
           samples ? sum / (double)samples : 0.0,
           (double)maximum,
           (double)delta,
           (unsigned long)samples);
    bool signal_detected = longest_qualified_run >= 3U;
    printf("  signal=%s (>= baseline + 8 dB for 3 samples; hits=%lu "
           "longest=%lu)\n",
           signal_detected ? "DETECTED" : "not detected",
           (unsigned long)qualified_samples,
           (unsigned long)longest_qualified_run);
    printf("  digital: GDO0 high=%lu edges=%lu; GDO2(CS) high=%lu edges=%lu; "
           "PKTSTATUS.CS=%lu\n",
           (unsigned long)gdo0_high,
           (unsigned long)gdo0_edges,
           (unsigned long)gdo2_high,
           (unsigned long)gdo2_edges,
           (unsigned long)pkt_cs_high);
    return signal_detected ? 0 : 1;
  }

  if (strcmp(argv[1], "survey") == 0) {
    static const uint32_t freqs[] = {315000000U, 433920000U, 868350000U, 915000000U};
    uint32_t duration = argc >= 3 ? (uint32_t)strtoul(argv[2], NULL, 0) : 12000U;
    if (duration < 1000U || duration > CC1101_MAX_TEST_MS) {
      printf("cc1101: survey duration must be 1000..60000 ms\n");
      return 1;
    }
    subghz_receiver_stop();
    vTaskDelay(pdMS_TO_TICKS(25));
    cc1101_set_preset(CC1101_PRESET_OOK_800KHZ, freqs[0]);

    float min_rssi[4] = {20.0f, 20.0f, 20.0f, 20.0f};
    float max_rssi[4] = {-140.0f, -140.0f, -140.0f, -140.0f};
    double sum_rssi[4] = {0};
    uint32_t counts[4] = {0};
    uint64_t end_us = (uint64_t)esp_timer_get_time() + ((uint64_t)duration * 1000ULL);
    printf("CC1101 band survey for %lu ms; press the key repeatedly now\n",
           (unsigned long)duration);
    while ((uint64_t)esp_timer_get_time() < end_us) {
      for (size_t i = 0; i < 4 && (uint64_t)esp_timer_get_time() < end_us; i++) {
        cc1101_strobe(CC1101_SIDLE);
        cc1101_set_frequency(freqs[i]);
        cc1101_strobe(CC1101_SRX);
        vTaskDelay(pdMS_TO_TICKS(3));
        for (int sample = 0; sample < 20; sample++) {
          float rssi = cc1101_convert_rssi(cc1101_status_reg(CC1101_RSSI));
          if (rssi < min_rssi[i])
            min_rssi[i] = rssi;
          if (rssi > max_rssi[i])
            max_rssi[i] = rssi;
          sum_rssi[i] += rssi;
          counts[i]++;
          vTaskDelay(pdMS_TO_TICKS(CC1101_RSSI_DELAY_MS));
        }
      }
    }
    cc1101_strobe(CC1101_SIDLE);
    printf("frequency_hz,min_dbm,avg_dbm,max_dbm\n");
    for (size_t i = 0; i < 4; i++) {
      printf("%lu,%.1f,%.1f,%.1f\n",
             (unsigned long)freqs[i],
             (double)min_rssi[i],
             counts[i] ? sum_rssi[i] / (double)counts[i] : 0.0,
             (double)max_rssi[i]);
    }
    return 0;
  }

  if (strcmp(argv[1], "capture") == 0) {
    uint32_t freq = argc >= 3 ? (uint32_t)strtoul(argv[2], NULL, 0) : CC1101_DEFAULT_TEST_HZ;
    uint32_t duration = argc >= 4 ? (uint32_t)strtoul(argv[3], NULL, 0) : 15000U;
    if (!cc1101_freq_supported(freq) || duration < 1000U || duration > CC1101_MAX_TEST_MS) {
      printf("cc1101: unsupported frequency or duration (1000..60000 ms)\n");
      return 1;
    }
    subghz_receiver_stop();
    vTaskDelay(pdMS_TO_TICKS(25));
    esp_err_t err = subghz_receiver_start(SUBGHZ_MODE_SCAN, CC1101_PRESET_OOK_270KHZ, freq);
    if (err != ESP_OK) {
      printf("cc1101: receiver start failed: %s\n", esp_err_to_name(err));
      return 1;
    }

    printf("CC1101 OOK capture: %lu Hz for %lu ms; press the key now\n",
           (unsigned long)freq,
           (unsigned long)duration);
    uint64_t end_us = (uint64_t)esp_timer_get_time() + ((uint64_t)duration * 1000ULL);
    uint32_t last_seq = 0;
    uint32_t captures = 0;
    while ((uint64_t)esp_timer_get_time() < end_us) {
      subghz_rx_result_t result = {0};
      if (subghz_receiver_get_result(&result) && result.seq != last_seq) {
        last_seq = result.seq;
        captures++;
        printf("capture #%lu: %s freq=%lu file=%s",
               (unsigned long)result.seq,
               result.decoded ? result.data.protocol_name : "raw/unknown",
               (unsigned long)result.freq,
               result.save_name[0] ? result.save_name : "not saved");
        if (!result.decoded && result.analysis.estimated_te != 0U) {
          printf(" TE~%lu us modulation=%s",
                 (unsigned long)result.analysis.estimated_te,
                 result.analysis.modulation_hint);
        }
        printf("\n");
      }
      vTaskDelay(pdMS_TO_TICKS(25));
    }
    subghz_receiver_stop();
    vTaskDelay(pdMS_TO_TICKS(50));
    cc1101_strobe(CC1101_SIDLE);
    printf("CC1101 capture finished: %lu event(s); radio IDLE\n", (unsigned long)captures);
    return captures > 0U ? 0 : 1;
  }

  if (strcmp(argv[1], "cscapture") == 0) {
    uint32_t freq = argc >= 3 ? (uint32_t)strtoul(argv[2], NULL, 0) : CC1101_DEFAULT_TEST_HZ;
    uint32_t duration = argc >= 4 ? (uint32_t)strtoul(argv[3], NULL, 0) : 8000U;
    if (!cc1101_freq_supported(freq) || duration < 1000U || duration > CC1101_MAX_TEST_MS) {
      printf("cc1101: unsupported frequency or duration (1000..60000 ms)\n");
      return 1;
    }

    subghz_receiver_stop();
    vTaskDelay(pdMS_TO_TICKS(25));
    esp_err_t err = subghz_receiver_start(SUBGHZ_MODE_RAW, CC1101_PRESET_OOK_270KHZ, freq);
    if (err != ESP_OK) {
      printf("cc1101: receiver start failed: %s\n", esp_err_to_name(err));
      return 1;
    }

    /* The receiver task first proves the normal asynchronous OOK profile and
     * arms RMT.  Only then replace serial-data output with carrier sense.  This
     * separates RF/AGC thresholding from the OOK decision circuit while using
     * the same physical GDO0 trace and capture peripheral. */
    for (uint32_t waited_ms = 0; !subghz_receiver_is_running() && waited_ms < 750U;
         waited_ms += 5U) {
      vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (!subghz_receiver_is_running()) {
      subghz_receiver_stop();
      printf("cc1101: receiver did not become ready for carrier-sense capture\n");
      return 1;
    }

    cc1101_strobe(CC1101_SIDLE);
    cc1101_write_reg(CC1101_IOCFG0, 0x0EU); /* Carrier sense on GDO0/GPIO8. */
    cc1101_strobe(CC1101_SRX);
    vTaskDelay(pdMS_TO_TICKS(5));
    uint8_t iocfg0 = cc1101_read_reg(CC1101_IOCFG0);
    uint8_t marc = cc1101_status_reg(CC1101_MARCSTATE) & 0x1FU;
    if (iocfg0 != 0x0EU || marc != 0x0DU) {
      subghz_receiver_stop();
      printf("cc1101: carrier-sense setup failed IOCFG0=%02X MARCSTATE=%02X\n", iocfg0, marc);
      return 1;
    }

    printf("CC1101 carrier-sense capture: %lu Hz for %lu ms; IOCFG0=%02X "
           "MARCSTATE=%02X\n",
           (unsigned long)freq,
           (unsigned long)duration,
           iocfg0,
           marc);
    uint64_t end_us = (uint64_t)esp_timer_get_time() + ((uint64_t)duration * 1000ULL);
    uint32_t last_seq = 0;
    uint32_t captures = 0;
    while ((uint64_t)esp_timer_get_time() < end_us) {
      subghz_rx_result_t result = {0};
      if (subghz_receiver_get_result(&result) && result.seq != last_seq) {
        last_seq = result.seq;
        captures++;
        printf("CS capture #%lu: freq=%lu file=%s\n",
               (unsigned long)result.seq,
               (unsigned long)result.freq,
               result.save_name[0] ? result.save_name : "not saved");
      }
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    subghz_receiver_stop();
    vTaskDelay(pdMS_TO_TICKS(50));
    cc1101_strobe(CC1101_SIDLE);
    printf("CC1101 carrier-sense capture finished: %lu event(s); radio IDLE\n",
           (unsigned long)captures);
    return captures > 0U ? 0 : 1;
  }

  printf("cc1101: unknown subcommand '%s'\n", argv[1]);
  return 1;
}

static void print_lf_report(const lf_rfid_field_report_t *r) {
  printf("LF field: %s, source=%s reference=%lu Hz generated=%lu Hz "
         "measured=%lu Hz\n",
         r->carrier_present ? "LOCKED" : "NO CARRIER FEEDBACK",
         r->field_active ? "High Boy" : "external/passive",
         (unsigned long)r->requested_hz,
         (unsigned long)r->generated_hz,
         (unsigned long)r->measured_hz);
  printf("  error=%ld ppm quality=%u%%; frequency measured by hardware PCNT\n",
         (long)r->frequency_error_ppm,
         r->quality_percent);
  printf("  CL rises=%lu; DA edges=%lu (%lu edges/s)\n",
         (unsigned long)r->carrier_edges,
         (unsigned long)r->data_edges,
         (unsigned long)r->data_edge_rate_hz);
  printf("  Duty/jitter and coil voltage/current require J7/J8 + scope/probe.\n");
}

static esp_err_t lf_start_default(uint32_t hz) {
  esp_err_t err = lf_rfid_init();
  if (err == ESP_OK) {
    err = lf_rfid_field_start(hz, LF_RFID_DEFAULT_DUTY_PERMILLE);
  }
  return err;
}

static int cmd_rfid(int argc, char **argv) {
  if (argc < 2) {
    printf("usage: rfid "
           "<status|bench|selftest|field|sweep|raw|read|watch|listen|modtest|"
           "on|off>\n");
    printf("  bench                         safe hardware bring-up; Proxmark "
           "field must be OFF\n");
    printf("  field [window_ms]             short 125 kHz feedback/quality "
           "measurement\n");
    printf("  sweep [start end step win_ms] sweep resonance region (defaults "
           "118k..132k)\n");
    printf("  raw [duration_ms]             capture 125KHz_DA transition "
           "intervals\n");
    printf("  read [timeout_ms]             read parity-checked EM4100/EM4102\n");
    printf("  watch [seconds]               live EM4100 reader loop (default "
           "10, max 60 s)\n");
    printf("  listen [window_ms]            passively measure a Proxmark 125 "
           "kHz field\n");
    printf("  modtest [rate_hz duration_ms] test T2 load path in external "
           "field only\n");
    return 1;
  }

  if (strcmp(argv[1], "status") == 0) {
    printf("RF frontend: %s\n", rf_frontend_mode_name(rf_frontend_get_mode()));
    printf("LF: field=%s generated=%lu Hz EN(GPIO2)=%d MOD(GPIO11)=%d CL=%d "
           "DA=%d\n",
           lf_rfid_field_is_on() ? "ON" : "OFF",
           (unsigned long)lf_rfid_field_frequency(),
           gpio_get_level(GPIO_RFID_LF_COIL_PIN),
           gpio_get_level(GPIO_RFID_LF_MOD_PIN),
           gpio_get_level(GPIO_RFID_LF_CARRIER_PIN),
           gpio_get_level(GPIO_RFID_LF_DATA_PIN));
    return 0;
  }

  if (strcmp(argv[1], "off") == 0) {
    lf_rfid_field_stop();
    printf("LF field OFF\n");
    return 0;
  }

  if (strcmp(argv[1], "on") == 0) {
    uint32_t hz = argc >= 3 ? (uint32_t)strtoul(argv[2], NULL, 0) : LF_RFID_NOMINAL_HZ;
    esp_err_t err = lf_start_default(hz);
    if (err != ESP_OK) {
      printf("rfid: field start failed: %s (frontend=%s)\n",
             esp_err_to_name(err),
             rf_frontend_mode_name(rf_frontend_get_mode()));
      return 1;
    }
    printf("LF field ON at %lu Hz / 50%%. Use 'rfid off' when finished.\n",
           (unsigned long)lf_rfid_field_frequency());
    return 0;
  }

  if (strcmp(argv[1], "selftest") == 0) {
    lf_rfid_self_test_t test;
    esp_err_t err = lf_rfid_self_test(&test);
    printf("LF self-test: GPIO static=%s carrier=%s frequency=%s result=%s\n",
           test.gpio_static_ok ? "PASS" : "FAIL",
           test.carrier_present ? "PASS" : "FAIL",
           test.frequency_ok ? "PASS" : "FAIL",
           err == ESP_OK ? "PASS" : "FAIL");
    print_lf_report(&test.field);
    if (err != ESP_OK) {
      printf("  Check T2/T4/T5 Si2302 orientation, T3 pad 1/2 rework, coil "
             "continuity and J7/J8.\n");
      return 1;
    }
    return 0;
  }

  if (strcmp(argv[1], "bench") == 0) {
    esp_err_t err = lf_rfid_init();
    if (err != ESP_OK) {
      printf("LF bench: init FAIL (%s)\n", esp_err_to_name(err));
      return 1;
    }
    lf_rfid_field_stop();
    printf("LF bench: outputs safe EN(GPIO2)=%d MOD(GPIO11)=%d frontend=%s\n",
           gpio_get_level(GPIO_RFID_LF_COIL_PIN),
           gpio_get_level(GPIO_RFID_LF_MOD_PIN),
           rf_frontend_mode_name(rf_frontend_get_mode()));

    lf_rfid_field_report_t external = {0};
    err = lf_rfid_listen_external(30U, &external);
    if (err != ESP_OK) {
      printf("LF bench: passive precheck FAIL (%s)\n", esp_err_to_name(err));
      return 1;
    }
    if (external.carrier_present) {
      print_lf_report(&external);
      printf("LF bench: active test SKIPPED. Stop the Proxmark reader field "
             "and retry.\n");
      return 1;
    }

    lf_rfid_self_test_t test = {0};
    err = lf_rfid_self_test(&test);
    printf("LF bench: GPIO=%s carrier feedback=%s frequency=%s result=%s; "
           "field now OFF\n",
           test.gpio_static_ok ? "PASS" : "FAIL",
           test.carrier_present ? "PASS" : "FAIL",
           test.frequency_ok ? "PASS" : "FAIL",
           err == ESP_OK ? "PASS" : "FAIL");
    print_lf_report(&test.field);
    if (err != ESP_OK) {
      printf("  Check T2/T4/T5 Si2302 orientation, T3 pad 1/2 rework, coil and "
             "J7/J8.\n");
      return 1;
    }
    printf("Next: Proxmark simulates EM4100, then run 'rfid watch 10'.\n");
    return 0;
  }

  if (strcmp(argv[1], "field") == 0) {
    uint32_t window = argc >= 3 ? (uint32_t)strtoul(argv[2], NULL, 0) : LF_DEFAULT_MEASURE_MS;
    esp_err_t err = lf_start_default(LF_RFID_NOMINAL_HZ);
    if (err == ESP_OK) {
      vTaskDelay(pdMS_TO_TICKS(5));
      lf_rfid_field_report_t report;
      err = lf_rfid_measure(window, &report);
      lf_rfid_field_stop();
      if (err == ESP_OK) {
        print_lf_report(&report);
        return report.carrier_present ? 0 : 1;
      }
    }
    lf_rfid_field_stop();
    printf("rfid: field test failed: %s\n", esp_err_to_name(err));
    return 1;
  }

  if (strcmp(argv[1], "sweep") == 0) {
    uint32_t start = argc >= 3 ? (uint32_t)strtoul(argv[2], NULL, 0) : 118000U;
    uint32_t end = argc >= 4 ? (uint32_t)strtoul(argv[3], NULL, 0) : 132000U;
    uint32_t step = argc >= 5 ? (uint32_t)strtoul(argv[4], NULL, 0) : 1000U;
    uint32_t window = argc >= 6 ? (uint32_t)strtoul(argv[5], NULL, 0) : 20U;
    if (start < LF_RFID_MIN_HZ || end > LF_RFID_MAX_HZ || start > end || step == 0) {
      printf("rfid: invalid sweep; allowed %u..%u Hz\n", LF_RFID_MIN_HZ, LF_RFID_MAX_HZ);
      return 1;
    }
    printf("requested_hz,generated_hz,measured_hz,error_ppm,quality,data_edges\n");
    for (uint32_t hz = start; hz <= end; hz += step) {
      lf_rfid_field_report_t r = {0};
      esp_err_t err = lf_start_default(hz);
      if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(3));
        err = lf_rfid_measure(window, &r);
      }
      lf_rfid_field_stop();
      if (err != ESP_OK) {
        printf("%lu,ERROR,%s\n", (unsigned long)hz, esp_err_to_name(err));
        return 1;
      }
      printf("%lu,%lu,%lu,%ld,%u,%lu\n",
             (unsigned long)r.requested_hz,
             (unsigned long)r.generated_hz,
             (unsigned long)r.measured_hz,
             (long)r.frequency_error_ppm,
             r.quality_percent,
             (unsigned long)r.data_edges);
      if (UINT32_MAX - hz < step) {
        break;
      }
    }
    printf("Sweep is digital loopback characterization, not an absolute "
           "antenna-amplitude sweep.\n");
    return 0;
  }

  if (strcmp(argv[1], "raw") == 0) {
    uint32_t duration = argc >= 3 ? (uint32_t)strtoul(argv[2], NULL, 0) : 50U;
    lf_rfid_edge_t edges[128];
    size_t count = 0;
    bool started_here = !lf_rfid_field_is_on();
    esp_err_t err = started_here ? lf_start_default(LF_RFID_NOMINAL_HZ) : ESP_OK;
    if (err == ESP_OK) {
      err = lf_rfid_capture_edges(duration, edges, 128, &count);
    }
    if (started_here) {
      lf_rfid_field_stop();
    }
    if (err != ESP_OK) {
      printf("rfid: raw capture failed: %s\n", esp_err_to_name(err));
      return 1;
    }
    printf("DA edges captured=%u (delta_us,level):\n", (unsigned)count);
    for (size_t i = 0; i < count; i++) {
      printf(
          "%lu,%u%s", (unsigned long)edges[i].delta_us, edges[i].level, (i % 8 == 7) ? "\n" : "  ");
    }
    if ((count % 8) != 0) {
      printf("\n");
    }
    return 0;
  }

  if (strcmp(argv[1], "read") == 0) {
    uint32_t timeout = argc >= 3 ? (uint32_t)strtoul(argv[2], NULL, 0) : LF_DEFAULT_READ_MS;
    bool started_here = !lf_rfid_field_is_on();
    esp_err_t err = started_here ? lf_start_default(LF_RFID_NOMINAL_HZ) : ESP_OK;
    lf_rfid_em4100_t card = {0};
    if (err == ESP_OK) {
      err = lf_rfid_read_em4100(timeout, &card);
    }
    if (started_here) {
      lf_rfid_field_stop();
    }
    if (err != ESP_OK) {
      printf("rfid: no valid EM4100/EM4102 frame: %s\n", esp_err_to_name(err));
      return 1;
    }
    printf("EM4100: %02X%02X%02X%02X%02X  decimal=%llu parity=PASS polarity=%s\n",
           card.bytes[0],
           card.bytes[1],
           card.bytes[2],
           card.bytes[3],
           card.bytes[4],
           (unsigned long long)card.id,
           card.inverted ? "inverted" : "normal");
    return 0;
  }

  if (strcmp(argv[1], "watch") == 0) {
    uint32_t seconds = argc >= 3 ? (uint32_t)strtoul(argv[2], NULL, 0) : LF_DEFAULT_WATCH_S;
    if (seconds == 0 || seconds > LF_MAX_WATCH_S) {
      printf("rfid: watch duration must be 1..%u seconds\n", LF_MAX_WATCH_S);
      return 1;
    }

    esp_err_t err = lf_start_default(LF_RFID_NOMINAL_HZ);
    if (err != ESP_OK) {
      printf("rfid: reader field start failed: %s\n", esp_err_to_name(err));
      return 1;
    }
    printf("RFID live reader: %lu s; High Boy field ON. Present a tag or "
           "Proxmark emulator.\n",
           (unsigned long)seconds);
    const int64_t started_us = esp_timer_get_time();
    const int64_t end_us = started_us + (int64_t)seconds * 1000000LL;
    int64_t next_heartbeat_us = started_us + 1000000LL;
    uint64_t last_id = UINT64_MAX;
    uint32_t attempts = 0;
    uint32_t valid_frames = 0;
    while (esp_timer_get_time() < end_us) {
      lf_rfid_em4100_t card = {0};
      err = lf_rfid_read_em4100(LF_DEFAULT_READ_MS, &card);
      attempts++;
      if (err == ESP_OK) {
        valid_frames++;
        if (card.id != last_id) {
          printf("EM4100 LIVE: %02X%02X%02X%02X%02X decimal=%llu polarity=%s\n",
                 card.bytes[0],
                 card.bytes[1],
                 card.bytes[2],
                 card.bytes[3],
                 card.bytes[4],
                 (unsigned long long)card.id,
                 card.inverted ? "inverted" : "normal");
          last_id = card.id;
        }
      } else if (err != ESP_ERR_NOT_FOUND) {
        printf("rfid: live reader stopped on error: %s\n", esp_err_to_name(err));
        break;
      }
      if (esp_timer_get_time() >= next_heartbeat_us) {
        printf("  live: attempts=%lu valid=%lu last=%s\n",
               (unsigned long)attempts,
               (unsigned long)valid_frames,
               last_id == UINT64_MAX ? "none" : "seen");
        next_heartbeat_us += 1000000LL;
      }
    }
    lf_rfid_field_stop();
    printf("RFID live reader finished: attempts=%lu valid=%lu "
           "unique/latest=%s; field OFF\n",
           (unsigned long)attempts,
           (unsigned long)valid_frames,
           last_id == UINT64_MAX ? "none" : "captured");
    return valid_frames > 0U ? 0 : 1;
  }

  if (strcmp(argv[1], "listen") == 0) {
    uint32_t window = argc >= 3 ? (uint32_t)strtoul(argv[2], NULL, 0) : 100U;
    esp_err_t err = lf_rfid_init();
    if (err == ESP_OK && lf_rfid_field_is_on()) {
      err = ESP_ERR_INVALID_STATE;
    }
    lf_rfid_field_report_t external = {0};
    if (err == ESP_OK) {
      err = lf_rfid_listen_external(window, &external);
    }
    if (err != ESP_OK) {
      printf("rfid: passive listen failed: %s (turn High Boy field OFF)\n", esp_err_to_name(err));
      return 1;
    }
    print_lf_report(&external);
    printf("High Boy outputs stayed OFF during the passive measurement.\n");
    return external.carrier_present ? 0 : 1;
  }

  if (strcmp(argv[1], "modtest") == 0) {
    uint32_t rate = argc >= 3 ? (uint32_t)strtoul(argv[2], NULL, 0) : LF_MOD_TEST_RATE_HZ;
    uint32_t duration = argc >= 4 ? (uint32_t)strtoul(argv[3], NULL, 0) : LF_MOD_TEST_MS;
    esp_err_t err = lf_rfid_init();
    lf_rfid_field_report_t external = {0};
    if (err == ESP_OK) {
      err = lf_rfid_modulation_test(rate, duration, &external);
    }
    if (err != ESP_OK) {
      printf("rfid: T2 modulation-path test refused/failed: %s\n", esp_err_to_name(err));
      if (err == ESP_ERR_NOT_FOUND) {
        printf("  Start the Proxmark LF reader field, place both coils "
               "together, then retry.\n");
      }
      return 1;
    }
    print_lf_report(&external);
    printf("T2 load-path test: PASS command path, %lu Hz for %lu ms; High Boy "
           "carrier stayed OFF.\n",
           (unsigned long)rate,
           (unsigned long)duration);
    printf("This is an engineering modulation pulse, not yet a protocol-valid "
           "EM4100 tag.\n");
    return 0;
  }

  printf("rfid: unknown subcommand '%s'\n", argv[1]);
  return 1;
}

static esp_err_t ensure_nfc(void) {
  if (highboy_nfc_is_initialized()) {
    return ESP_OK;
  }
  highboy_nfc_config_t cfg = HIGHBOY_NFC_CONFIG_DEFAULT();
  return highboy_nfc_init(&cfg);
}

static const char *protocol_name(hb_nfc_protocol_t protocol) {
  switch (protocol) {
    case HB_PROTO_ISO14443_3A:
      return "NFC-A / ISO14443-3A";
    case HB_PROTO_ISO14443_3B:
      return "NFC-B / ISO14443-3B";
    case HB_PROTO_ISO14443_4A:
      return "ISO-DEP A";
    case HB_PROTO_ISO14443_4B:
      return "ISO-DEP B";
    case HB_PROTO_FELICA:
      return "NFC-F / FeliCa";
    case HB_PROTO_ISO15693:
      return "NFC-V / ISO15693";
    case HB_PROTO_MF_CLASSIC:
      return "MIFARE Classic";
    case HB_PROTO_MF_ULTRALIGHT:
      return "MIFARE Ultralight";
    case HB_PROTO_MF_PLUS:
      return "MIFARE Plus";
    default:
      return "unknown";
  }
}

static void scanner_cb(nfc_scanner_event_t event, void *ctx) {
  (void)ctx;
  printf("NFC tag detected:");
  for (uint8_t i = 0; i < event.count; i++) {
    printf(" %s%s", protocol_name(event.protocols[i]), i + 1 < event.count ? "," : "");
  }
  printf("\n");
}

#define NFC_EMU_1K_BLOCKS    64
#define NFC_EMU_BLOCK_SIZE   16
#define NFC_EMU_DEFAULT_DUMP "/sdcard/nfc/dumps/mf_04F7D827.bin"

EXT_RAM_BSS_ATTR static mfc_emu_card_data_t s_console_emu_card;
static bool s_console_emu_running;

static esp_err_t load_mfc1k_dump(const char *path, mfc_emu_card_data_t *card) {
  if (path == NULL || card == NULL)
    return ESP_ERR_INVALID_ARG;

  FILE *fp = fopen(path, "rb");
  if (fp == NULL)
    return ESP_ERR_NOT_FOUND;

  memset(card, 0, sizeof(*card));
  size_t blocks = fread(card->blocks, NFC_EMU_BLOCK_SIZE, NFC_EMU_1K_BLOCKS, fp);
  int extra = fgetc(fp);
  fclose(fp);
  if (blocks != NFC_EMU_1K_BLOCKS || extra != EOF)
    return ESP_ERR_INVALID_SIZE;

  memcpy(card->uid, card->blocks[0], 4);
  card->uid_len = 4;
  card->atqa[0] = 0x04;
  card->atqa[1] = 0x00;
  card->sak = 0x08;
  card->type = MF_CLASSIC_1K;
  card->sector_count = 16;
  card->total_blocks = NFC_EMU_1K_BLOCKS;

  for (int sector = 0; sector < card->sector_count; sector++) {
    const uint8_t *trailer = card->blocks[(sector * 4) + 3];
    memcpy(card->keys[sector].key_a, trailer, 6);
    memcpy(card->keys[sector].key_b, trailer + 10, 6);
    card->keys[sector].key_a_known = true;
    card->keys[sector].key_b_known = true;
  }
  return ESP_OK;
}

static void print_mfc_emu_status(void) {
  mfc_emu_stats_t stats = mfc_emu_get_stats();
  printf("NFC emu: running=%s state=%s UID=%02X%02X%02X%02X auth=%d/%d "
         "reads=%d writes=%d "
         "field_losses=%d cycles=%d\n",
         s_console_emu_running ? "yes" : "no",
         mfc_emu_state_str(mfc_emu_get_state()),
         s_console_emu_card.uid[0],
         s_console_emu_card.uid[1],
         s_console_emu_card.uid[2],
         s_console_emu_card.uid[3],
         stats.successful_auths,
         stats.total_auths,
         stats.reads_served,
         stats.writes_served,
         stats.field_losses,
         stats.cycles);
}

static int cmd_nfc(int argc, char **argv) {
  if (argc < 2) {
    printf("usage: nfc "
           "<init|selftest|status|field|power|measure|detect|scan|emu|regs|"
           "deinit>\n");
    return 1;
  }

  if (strcmp(argv[1], "init") == 0) {
    esp_err_t err = ensure_nfc();
    printf("NFC init: %s\n", esp_err_to_name(err));
    return err == ESP_OK ? 0 : 1;
  }
  if (strcmp(argv[1], "deinit") == 0) {
    highboy_nfc_deinit();
    printf("NFC deinitialized and field OFF\n");
    return 0;
  }

  esp_err_t err = ensure_nfc();
  if (err != ESP_OK) {
    printf("nfc: ST25R3916B init failed: %s\n", esp_err_to_name(err));
    return 1;
  }

  if (strcmp(argv[1], "status") == 0) {
    uint8_t id = 0;
    err = highboy_nfc_ping(&id);
    uint8_t aux = 0;
    bool external = highboy_nfc_field_detected(&aux);
    printf("NFC: chip=%s ID=0x%02X field=%s IRQ(GPIO10)=%d AUX=0x%02X "
           "external=%s\n",
           err == ESP_OK ? "ST25R3916B" : "ERROR",
           id,
           st25r3916_core_field_is_on() ? "ON" : "OFF",
           gpio_get_level(GPIO_NFC_IRQ_PIN),
           aux,
           external ? "YES" : "NO");
    printf("RF frontend: %s\n", rf_frontend_mode_name(rf_frontend_get_mode()));
    return err == ESP_OK ? 0 : 1;
  }

  if (strcmp(argv[1], "field") == 0) {
    if (argc < 3 || (strcmp(argv[2], "on") != 0 && strcmp(argv[2], "off") != 0)) {
      printf("usage: nfc field <on|off>\n");
      return 1;
    }
    err = strcmp(argv[2], "on") == 0 ? highboy_nfc_field_on() : highboy_nfc_field_off();
    printf("NFC field %s: %s\n", argv[2], esp_err_to_name(err));
    return err == ESP_OK ? 0 : 1;
  }

  if (strcmp(argv[1], "emu") == 0) {
    if (argc < 3) {
      printf("usage: nfc emu <start [dump.bin]|status|stop>\n");
      return 1;
    }
    if (strcmp(argv[2], "status") == 0) {
      print_mfc_emu_status();
      return 0;
    }
    if (strcmp(argv[2], "stop") == 0) {
      if (s_console_emu_running) {
        nfc_listener_stop();
        s_console_emu_running = false;
      }
      print_mfc_emu_status();
      return 0;
    }
    if (strcmp(argv[2], "start") == 0) {
      const char *path = argc >= 4 ? argv[3] : NFC_EMU_DEFAULT_DUMP;
      if (s_console_emu_running) {
        nfc_listener_stop();
        s_console_emu_running = false;
      }
      err = load_mfc1k_dump(path, &s_console_emu_card);
      if (err != ESP_OK) {
        printf("NFC emu dump load failed: %s (%s)\n", esp_err_to_name(err), path);
        return 1;
      }
      hb_nfc_err_t emu_err = nfc_listener_start_emu(&s_console_emu_card);
      if (emu_err != HB_NFC_OK) {
        printf("NFC emu start failed: %d\n", emu_err);
        return 1;
      }
      s_console_emu_running = true;
      printf("NFC emu started: MIFARE Classic 1K UID=%02X%02X%02X%02X file=%s\n",
             s_console_emu_card.uid[0],
             s_console_emu_card.uid[1],
             s_console_emu_card.uid[2],
             s_console_emu_card.uid[3],
             path);
      print_mfc_emu_status();
      return 0;
    }
    printf("usage: nfc emu <start [dump.bin]|status|stop>\n");
    return 1;
  }

  if (strcmp(argv[1], "power") == 0) {
    if (argc >= 3) {
      if (strcmp(argv[2], "auto") == 0) {
        err = highboy_nfc_adjust_regulator_auto();
      } else {
        char *end = NULL;
        unsigned long mv = strtoul(argv[2], &end, 10);
        if (end == argv[2] || *end != '\0' || mv > UINT16_MAX) {
          printf("usage: nfc power [auto|2400..3400]\n");
          return 1;
        }
        err = highboy_nfc_set_regulator_mv((uint16_t)mv);
      }
      if (err != ESP_OK) {
        printf("NFC regulator configuration failed: %s\n", esp_err_to_name(err));
        return 1;
      }
    }

    uint16_t mv = 0;
    bool current_limit = false;
    bool manual = false;
    uint8_t raw = 0;
    err = highboy_nfc_get_regulator(&mv, &current_limit, &manual, &raw);
    if (err != ESP_OK) {
      printf("NFC regulator read failed: %s\n", esp_err_to_name(err));
      return 1;
    }
    printf("NFC regulator: %umV mode=%s current_limit=%s raw=0x%02X field=%s\n",
           mv,
           manual ? "manual" : "auto",
           current_limit ? "YES" : "no",
           raw,
           st25r3916_core_field_is_on() ? "ON" : "OFF");
    return 0;
  }

  if (strcmp(argv[1], "measure") == 0 || strcmp(argv[1], "selftest") == 0) {
    uint32_t samples = strcmp(argv[1], "selftest") == 0
                           ? 16U
                           : (argc >= 3 ? (uint32_t)strtoul(argv[2], NULL, 0) : 32U);
    if (samples == 0 || samples > 100) {
      printf("nfc: samples must be 1..100\n");
      return 1;
    }
    uint8_t id = 0;
    err = highboy_nfc_ping(&id);
    if (err == ESP_OK) {
      err = highboy_nfc_field_on();
    }
    uint8_t min = 0xFF, max = 0;
    uint32_t sum = 0;
    if (err == ESP_OK) {
      for (uint32_t i = 0; i < samples; i++) {
        uint8_t value = highboy_nfc_measure_amplitude();
        if (value < min)
          min = value;
        if (value > max)
          max = value;
        sum += value;
        vTaskDelay(pdMS_TO_TICKS(NFC_MEASURE_DELAY_MS));
      }
    }
    highboy_nfc_field_off();
    if (err != ESP_OK) {
      printf("NFC RF test failed: %s\n", esp_err_to_name(err));
      return 1;
    }
    printf("NFC RF %s: chip_id=0x%02X samples=%lu amplitude raw "
           "min/avg/max=%u/%lu/%u\n",
           strcmp(argv[1], "selftest") == 0 ? "self-test PASS" : "measurement",
           id,
           (unsigned long)samples,
           min,
           (unsigned long)(sum / samples),
           max);
    printf("  Raw ST25R3916B ADC values are comparative, not calibrated volts "
           "at the antenna.\n");
    return 0;
  }

  if (strcmp(argv[1], "detect") == 0) {
    highboy_nfc_field_off();
    vTaskDelay(pdMS_TO_TICKS(5));
    uint8_t aux = 0;
    bool detected = highboy_nfc_field_detected(&aux);
    printf("External 13.56 MHz field: %s (AUX_DISPLAY=0x%02X)\n",
           detected ? "DETECTED" : "not detected",
           aux);
    return detected ? 0 : 1;
  }

  if (strcmp(argv[1], "scan") == 0) {
    err = nfc_poller_start();
    if (err != ESP_OK) {
      printf("nfc: poller start failed: %s\n", esp_err_to_name(err));
      return 1;
    }

    /* Print the full NFC-A identity when possible; the generic scanner then
     * covers NFC-B/F/V if no Type A tag answered. */
    nfc_iso14443a_data_t card = {0};
    err = iso14443a_poller_select(&card);
    if (err == ESP_OK) {
      printf("NFC-A UID=");
      for (uint8_t i = 0; i < card.uid_len; i++) {
        printf("%02X", card.uid[i]);
      }
      printf(" ATQA=%02X%02X SAK=%02X\n", card.atqa[0], card.atqa[1], card.sak);
    } else {
      nfc_scanner_t *scanner = nfc_scanner_alloc();
      if (scanner == NULL) {
        err = ESP_ERR_NO_MEM;
      } else {
        err = nfc_scanner_start(scanner, scanner_cb, NULL);
        nfc_scanner_free(scanner);
      }
      if (err != ESP_OK) {
        printf("No NFC-A/B/F/V tag detected (%s)\n", esp_err_to_name(err));
      }
    }
    nfc_poller_stop();
    return err == ESP_OK ? 0 : 1;
  }

  if (strcmp(argv[1], "regs") == 0) {
    nfc_debug_dump_regs();
    printf("NFC register dump written to firmware log\n");
    return 0;
  }

  printf("nfc: unknown subcommand '%s'\n", argv[1]);
  return 1;
}

void register_rf_commands(void) {
  const esp_console_cmd_t lora = {
      .command = "lora",
      .help = "SX1262 LoRa bring-up, RX/RSSI and controlled low-power TX lab",
      .hint = NULL,
      .func = &cmd_lora,
  };
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&lora));

  const esp_console_cmd_t cc1101 = {
      .command = "cc1101",
      .help = "CC1101 RX-only RSSI survey and OOK capture diagnostics",
      .hint = NULL,
      .func = &cmd_cc1101,
  };
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&cc1101));

  const esp_console_cmd_t rfid = {
      .command = "rfid",
      .help = "Native 125 kHz RFID diagnostics and EM4100 reader",
      .hint = NULL,
      .func = &cmd_rfid,
  };
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&rfid));

  const esp_console_cmd_t nfc = {
      .command = "nfc",
      .help = "ST25R3916B NFC RF diagnostics and tag scan",
      .hint = NULL,
      .func = &cmd_nfc,
  };
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&nfc));
}
