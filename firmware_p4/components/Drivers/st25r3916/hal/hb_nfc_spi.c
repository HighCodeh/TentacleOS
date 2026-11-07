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

#include "hb_nfc_spi.h"
#include "hb_nfc_v_codec.h"
#include "hb_nfc_v_rx.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"

#include <string.h>

#include "driver/spi_master.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "esp_rom_gpio.h"
#include "soc/spi_periph.h"
#include "soc/gpio_struct.h"
#include "soc/gpio_sig_map.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_err.h"

#include "spi.h"
#include "rf_frontend.h"
#include "st25r3916_reg.h"
#include "st25r3916_cmd.h"

static const char *TAG = "hb_nfc_spi";

/* ST25R3916 SPI framing constants (datasheet DocID 031020 Rev 3, Sec. 6). */
#define HB_NFC_SPI_READ_FLAG        0x40 /**< Set bit 6 to select a read operation. */
#define HB_NFC_SPI_ADDR_MASK        0x3F /**< Register address occupies bits [5:0]. */
#define HB_NFC_SPI_REG_XFER_BITS    16   /**< Bit count for a 2-byte register transaction. */
#define HB_NFC_SPI_CMD_XFER_BITS    8    /**< Bit count for a 1-byte direct command. */
#define HB_NFC_SPI_FIFO_LOAD_BYTE   0x80 /**< FIFO load prefix byte. */
#define HB_NFC_SPI_FIFO_READ_BYTE   0x9F /**< FIFO read prefix byte. */
#define HB_NFC_SPI_PT_MEM_READ_BYTE 0xBF /**< Passive target memory read prefix byte. */
#define HB_NFC_SPI_FIFO_MAX_BYTES   512  /**< ST25R3916 FIFO capacity. */
#define HB_NFC_SPI_PT_MEM_MAX_LEN   19   /**< Maximum PT memory payload (NFC-F section). */
#define HB_NFC_SPI_PT_MEM_HDR_BYTES 2    /**< Bytes before payload in PT memory read response. */
#define HB_NFC_SPI_RAW_XFER_MAX_LEN 64   /**< Maximum length for hb_nfc_spi_raw_xfer(). */

#define HB_NFC_TX_RESOLUTION_HZ   40000000U
#define HB_NFC_RF_CARRIER_HZ      13560000U
#define HB_NFC_TX_MAX_BITS        162U
#define HB_NFC_PULSE_HIGH_CYCLES  64U
#define HB_NFC_PULSE_MAX_CYCLES   4096U
#define HB_NFC_TX_SYMBOLS_PER_BIT 5U
/* Full 162-bit reply plus framing and DMA alignment; no mid-frame refill. */
#define HB_NFC_TX_DMA_SYMBOLS          832U
#define HB_NFC_TX_TIMEOUT_MS           20
#define HB_NFC_V_FDT_US                319U
#define HB_NFC_V_LATE_US               50U
#define HB_NFC_V_FRAME_OVERHEAD_CYCLES 2560U
#define HB_NFC_V_BIT_CYCLES            512U
#define HB_NFC_V_LOW_RATE_COEFFICIENT  4U
#define HB_NFC_MS_PER_SECOND           1000U
#define HB_NFC_BUS_TIMEOUT_MS          100
#define HB_NFC_SUBCARRIER_HALF_CYCLES  8U
#define HB_NFC_QUIET_HALF_CYCLES       32U
#define HB_NFC_BITS_PER_BYTE           8U

static rmt_channel_handle_t s_tx_channel;
static bool s_tx_enabled;
static rmt_encoder_handle_t s_tx_encoder;
static rmt_encoder_handle_t s_v_encoder;
static hb_nfc_v_codec_tx_t s_v_tx;
static uint8_t s_v_frame[HB_NFC_V_CODEC_MAX_FRAME];
static uint8_t s_v_rx_frame[HB_NFC_V_CODEC_MAX_FRAME];
static uint32_t s_v_remainder;
static rmt_symbol_word_t s_tx_symbols[(HB_NFC_TX_MAX_BITS + 2) * HB_NFC_TX_SYMBOLS_PER_BIT];
#define HB_NFC_RX_RESOLUTION_HZ 1000000U
#define HB_NFC_RX_SYMBOLS       1024U
#define HB_NFC_RX_IDLE_NS       200000U
#define HB_NFC_RX_GLITCH_NS     1000U
#define HB_NFC_RX_WINDOW_MS     20U
#define HB_NFC_RX_REFRACTORY_US 12U
#define HB_NFC_RX_GAPS_MAX      127U
#define HB_NFC_RX_QUEUE_DEPTH   1U

static rmt_channel_handle_t s_rx_channel;
static bool s_rx_enabled;
static rmt_symbol_word_t s_rx_symbols[HB_NFC_RX_SYMBOLS];
static StaticQueue_t s_rx_queue_state;
static uint8_t s_rx_queue_storage[sizeof(size_t)];
static QueueHandle_t s_rx_queue;
static gpio_num_t s_miso;
static gpio_num_t s_mosi;
static uint32_t s_tx_signal;
static bool s_has_tx_signal;

static spi_device_handle_t s_spi = NULL;
static bool s_is_init = false;
static int s_spi_host = -1;

static esp_err_t transmit(spi_transaction_t *t);
static esp_err_t transmit_target(size_t count);
static esp_err_t transmit_target_encoded(rmt_encoder_handle_t encoder,
                                         const void *data,
                                         size_t size,
                                         int timeout_ms);

esp_err_t hb_nfc_spi_init(const hb_nfc_spi_config_t *config) {
  if (config == NULL)
    return ESP_ERR_INVALID_ARG;
  if (s_is_init)
    return ESP_OK;

  esp_err_t ret = spi_bus_init(
      (spi_host_device_t)config->spi_host, config->pin_mosi, config->pin_miso, config->pin_sclk);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "bus init fail: %s", esp_err_to_name(ret));
    return ret;
  }

  spi_device_config_t dev = {
      .clock_speed_hz = config->clock_hz,
      .mode = config->mode,
      .cs_pin = config->pin_cs,
      .queue_size = 1,
  };
  ret = spi_add_device((spi_host_device_t)config->spi_host, SPI_DEVICE_NFC, &dev);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "add device fail: %s", esp_err_to_name(ret));
    return ret;
  }
  s_spi = spi_get_handle(SPI_DEVICE_NFC);
  if (s_spi == NULL) {
    return ESP_FAIL;
  }

  s_mosi = config->pin_mosi;
  s_miso = config->pin_miso;
  s_spi_host = config->spi_host;
  s_is_init = true;
  ESP_LOGI(TAG,
           "SPI OK: mode=%u clk=%lu cs=%d",
           config->mode,
           (unsigned long)config->clock_hz,
           config->pin_cs);
  return ESP_OK;
}

esp_err_t hb_nfc_spi_deinit(void) {
  if (!s_is_init)
    return ESP_OK;
  esp_err_t rx_error = hb_nfc_v_rx_deinit();
  if (rx_error != ESP_OK)
    return rx_error;
  const bool shared_lock = s_spi_host == SPI3_HOST;
  if (shared_lock && !spi_bus_lock_take(HB_NFC_BUS_TIMEOUT_MS))
    return ESP_ERR_TIMEOUT;
  esp_err_t err = spi_device_acquire_bus(s_spi, portMAX_DELAY);
  if (err != ESP_OK)
    goto unlock;
  if (s_rx_channel != NULL) {
    if (s_rx_enabled) {
      err = rmt_disable(s_rx_channel);
      if (err != ESP_OK)
        goto release;
      s_rx_enabled = false;
    }
    err = rmt_del_channel(s_rx_channel);
    if (err != ESP_OK)
      goto release;
    s_rx_channel = NULL;
  }
  if (s_tx_channel != NULL) {
    if (s_tx_enabled) {
      err = rmt_disable(s_tx_channel);
      if (err != ESP_OK)
        goto release;
      s_tx_enabled = false;
    }
    err = rmt_del_channel(s_tx_channel);
    if (err != ESP_OK)
      goto release;
    s_tx_channel = NULL;
    s_has_tx_signal = false;
    gpio_set_direction(s_mosi, GPIO_MODE_INPUT_OUTPUT);
    esp_rom_gpio_connect_out_signal(s_mosi, spi_periph_signal[s_spi_host].spid_out, false, false);
  }
  if (s_v_encoder != NULL) {
    err = rmt_del_encoder(s_v_encoder);
    if (err != ESP_OK)
      goto release;
    s_v_encoder = NULL;
  }
  if (s_tx_encoder != NULL) {
    err = rmt_del_encoder(s_tx_encoder);
    if (err != ESP_OK)
      goto release;
    s_tx_encoder = NULL;
  }
release:
  spi_device_release_bus(s_spi);
  if (err == ESP_OK)
    err = spi_remove_device(SPI_DEVICE_NFC);
unlock:
  if (shared_lock)
    spi_bus_lock_give();
  if (err != ESP_OK)
    return err;
  s_spi = NULL;
  s_spi_host = -1;
  s_is_init = false;
  return ESP_OK;
}

esp_err_t hb_nfc_spi_reg_read(uint8_t addr, uint8_t *out_value) {
  if (out_value == NULL)
    return ESP_ERR_INVALID_ARG;

  uint8_t tx[2] = {(uint8_t)(HB_NFC_SPI_READ_FLAG | (addr & HB_NFC_SPI_ADDR_MASK)), 0x00};
  uint8_t rx[2] = {0};
  spi_transaction_t t = {
      .length = HB_NFC_SPI_REG_XFER_BITS,
      .tx_buffer = tx,
      .rx_buffer = rx,
  };
  esp_err_t err = transmit(&t);
  if (err != ESP_OK) {
    *out_value = 0;
    return err;
  }
  *out_value = rx[1];
  return ESP_OK;
}

esp_err_t hb_nfc_spi_reg_write(uint8_t addr, uint8_t value) {
  const bool controls_field = (addr & HB_NFC_SPI_ADDR_MASK) == ST25R3916_REG_OP_CTRL;
  const bool enables_tx = controls_field && (value & ST25R3916_OP_CTRL_TX_EN) != 0;
  if (enables_tx) {
    esp_err_t claim = rf_frontend_claim(RF_FRONTEND_NFC_13MHZ);
    if (claim != ESP_OK) {
      ESP_LOGW(TAG, "refusing NFC TX: shared antenna is busy");
      return claim;
    }
  }

  uint8_t tx[2] = {(uint8_t)(addr & HB_NFC_SPI_ADDR_MASK), value};
  spi_transaction_t t = {
      .length = HB_NFC_SPI_REG_XFER_BITS,
      .tx_buffer = tx,
  };
  esp_err_t err = transmit(&t);
  if (controls_field && !enables_tx && err == ESP_OK) {
    rf_frontend_release(RF_FRONTEND_NFC_13MHZ);
  }
  return err;
}

esp_err_t hb_nfc_spi_reg_read_b(uint8_t addr, uint8_t *out_value) {
  if (out_value == NULL)
    return ESP_ERR_INVALID_ARG;

  uint8_t tx[3] = {
      ST25R3916_SPI_SPACE_B,
      (uint8_t)(HB_NFC_SPI_READ_FLAG | (addr & HB_NFC_SPI_ADDR_MASK)),
      0x00,
  };
  uint8_t rx[3] = {0};
  spi_transaction_t t = {
      .length = 24,
      .tx_buffer = tx,
      .rx_buffer = rx,
  };
  esp_err_t err = transmit(&t);
  if (err != ESP_OK) {
    *out_value = 0;
    return err;
  }
  *out_value = rx[2];
  return ESP_OK;
}

esp_err_t hb_nfc_spi_reg_write_b(uint8_t addr, uint8_t value) {
  uint8_t tx[3] = {
      ST25R3916_SPI_SPACE_B,
      (uint8_t)(addr & HB_NFC_SPI_ADDR_MASK),
      value,
  };
  spi_transaction_t t = {
      .length = 24,
      .tx_buffer = tx,
  };
  return transmit(&t);
}

esp_err_t hb_nfc_spi_reg_modify(uint8_t addr, uint8_t mask, uint8_t value) {
  uint8_t cur;
  esp_err_t err = hb_nfc_spi_reg_read(addr, &cur);
  if (err != ESP_OK)
    return err;
  uint8_t nv = (cur & (uint8_t)~mask) | (value & mask);
  return hb_nfc_spi_reg_write(addr, nv);
}

esp_err_t hb_nfc_spi_fifo_load(const uint8_t *data, size_t len) {
  if (data == NULL || len == 0 || len > HB_NFC_SPI_FIFO_MAX_BYTES)
    return ESP_ERR_INVALID_ARG;

  static uint8_t tx[1 + HB_NFC_SPI_FIFO_MAX_BYTES];
  tx[0] = HB_NFC_SPI_FIFO_LOAD_BYTE;
  memcpy(&tx[1], data, len);

  spi_transaction_t t = {
      .length = (uint32_t)((len + 1) * 8),
      .tx_buffer = tx,
  };
  return transmit(&t);
}

esp_err_t hb_nfc_spi_fifo_read(uint8_t *data, size_t len) {
  if (data == NULL || len == 0 || len > HB_NFC_SPI_FIFO_MAX_BYTES)
    return ESP_ERR_INVALID_ARG;

  static uint8_t tx[1 + HB_NFC_SPI_FIFO_MAX_BYTES];
  static uint8_t rx[1 + HB_NFC_SPI_FIFO_MAX_BYTES];
  memset(tx, 0, len + 1);
  tx[0] = HB_NFC_SPI_FIFO_READ_BYTE;

  spi_transaction_t t = {
      .length = (uint32_t)((len + 1) * 8),
      .tx_buffer = tx,
      .rx_buffer = rx,
  };
  esp_err_t err = transmit(&t);
  if (err != ESP_OK)
    return err;
  memcpy(data, &rx[1], len);
  return ESP_OK;
}

esp_err_t hb_nfc_spi_direct_cmd(uint8_t cmd) {
  spi_transaction_t t = {
      .length = HB_NFC_SPI_CMD_XFER_BITS,
      .tx_buffer = &cmd,
  };
  return transmit(&t);
}

esp_err_t hb_nfc_spi_pt_mem_write(uint8_t prefix, const uint8_t *data, size_t len) {
  if (data == NULL || len == 0 || len > HB_NFC_SPI_PT_MEM_MAX_LEN)
    return ESP_ERR_INVALID_ARG;

  uint8_t tx[1 + HB_NFC_SPI_PT_MEM_MAX_LEN];
  tx[0] = prefix;
  memcpy(&tx[1], data, len);

  spi_transaction_t t = {
      .length = (uint32_t)((len + 1) * 8),
      .tx_buffer = tx,
  };
  return transmit(&t);
}

esp_err_t hb_nfc_spi_pt_mem_read(uint8_t *out_data, size_t len) {
  if (out_data == NULL || len == 0 || len > HB_NFC_SPI_PT_MEM_MAX_LEN)
    return ESP_ERR_INVALID_ARG;

  uint8_t tx[HB_NFC_SPI_PT_MEM_HDR_BYTES + HB_NFC_SPI_PT_MEM_MAX_LEN];
  uint8_t rx[HB_NFC_SPI_PT_MEM_HDR_BYTES + HB_NFC_SPI_PT_MEM_MAX_LEN];
  memset(tx, 0, sizeof(tx));
  tx[0] = HB_NFC_SPI_PT_MEM_READ_BYTE;

  spi_transaction_t t = {
      .length = (uint32_t)((len + HB_NFC_SPI_PT_MEM_HDR_BYTES) * 8),
      .tx_buffer = tx,
      .rx_buffer = rx,
  };
  esp_err_t err = transmit(&t);
  if (err != ESP_OK)
    return err;
  memcpy(out_data, &rx[HB_NFC_SPI_PT_MEM_HDR_BYTES], len);
  return ESP_OK;
}

esp_err_t hb_nfc_spi_raw_xfer(const uint8_t *tx, uint8_t *rx, size_t len) {
  if (tx == NULL || len == 0 || len > HB_NFC_SPI_RAW_XFER_MAX_LEN)
    return ESP_ERR_INVALID_ARG;

  spi_transaction_t t = {
      .length = (uint32_t)(len * 8),
      .tx_buffer = tx,
      .rx_buffer = rx,
  };
  return transmit(&t);
}

esp_err_t hb_nfc_spi_prepare_target_tx(void) {
  if (!s_is_init || !rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ))
    return ESP_ERR_INVALID_STATE;
  if (s_tx_channel != NULL)
    return ESP_OK;
  bool shared_lock = s_spi_host == SPI3_HOST;
  if (shared_lock && !spi_bus_lock_take(HB_NFC_BUS_TIMEOUT_MS))
    return ESP_ERR_TIMEOUT;
  esp_err_t err = spi_device_acquire_bus(s_spi, portMAX_DELAY);
  if (err != ESP_OK)
    goto unlock;
  rmt_copy_encoder_config_t encoder_config = {};
  err = rmt_new_copy_encoder(&encoder_config, &s_tx_encoder);
  if (err != ESP_OK)
    goto release;
  rmt_tx_channel_config_t config = {
      .gpio_num = s_mosi,
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = HB_NFC_TX_RESOLUTION_HZ,
      .mem_block_symbols = HB_NFC_TX_DMA_SYMBOLS,
      .trans_queue_depth = 1,
      .flags.with_dma = true,
  };
  err = rmt_new_tx_channel(&config, &s_tx_channel);
  if (err == ESP_OK) {
    s_tx_signal = GPIO.func_out_sel_cfg[s_mosi].out_sel;
    s_has_tx_signal = true;
  }
  /* Channel allocation routes MOSI to RMT; retain SPI until a target reply. */
  gpio_set_direction(s_mosi, GPIO_MODE_INPUT_OUTPUT);
  esp_rom_gpio_connect_out_signal(s_mosi, spi_periph_signal[s_spi_host].spid_out, false, false);
  if (err != ESP_OK) {
    rmt_del_encoder(s_tx_encoder);
    s_tx_encoder = NULL;
    ESP_LOGE(TAG, "Target TX allocation failed: %s", esp_err_to_name(err));
  }
release:
  spi_device_release_bus(s_spi);
unlock:
  if (shared_lock)
    spi_bus_lock_give();
  return err;
}

static bool
receive_done(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *event, void *ctx) {
  (void)channel;
  (void)ctx;
  BaseType_t wake = pdFALSE;
  size_t count = event->num_symbols;
  xQueueSendFromISR(s_rx_queue, &count, &wake);
  return wake == pdTRUE;
}

esp_err_t hb_nfc_spi_capture_gaps(uint16_t *out_gaps, size_t capacity, size_t *out_count) {
  if (out_gaps == NULL || out_count == NULL || capacity < HB_NFC_RX_GAPS_MAX)
    return ESP_ERR_INVALID_ARG;
  *out_count = 0;
  if (!s_is_init || !rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ))
    return ESP_ERR_INVALID_STATE;
  bool shared_lock = s_spi_host == SPI3_HOST;
  if (shared_lock && !spi_bus_lock_take(HB_NFC_BUS_TIMEOUT_MS))
    return ESP_ERR_TIMEOUT;
  esp_err_t err = spi_device_acquire_bus(s_spi, portMAX_DELAY);
  if (err != ESP_OK)
    goto unlock;
  bool transparent = false;
  if (s_rx_channel != NULL) {
    err = ESP_ERR_INVALID_STATE;
    goto release;
  }
  if (s_rx_queue == NULL)
    s_rx_queue = xQueueCreateStatic(
        HB_NFC_RX_QUEUE_DEPTH, sizeof(size_t), s_rx_queue_storage, &s_rx_queue_state);
  xQueueReset(s_rx_queue);
  if (s_rx_channel == NULL) {
    rmt_rx_channel_config_t config = {.gpio_num = s_miso,
                                      .clk_src = RMT_CLK_SRC_DEFAULT,
                                      .resolution_hz = HB_NFC_RX_RESOLUTION_HZ,
                                      .mem_block_symbols = HB_NFC_RX_SYMBOLS,
                                      .flags.with_dma = true};
    err = rmt_new_rx_channel(&config, &s_rx_channel);
    if (err != ESP_OK)
      goto release;
    rmt_rx_event_callbacks_t callbacks = {.on_recv_done = receive_done};
    err = rmt_rx_register_event_callbacks(s_rx_channel, &callbacks, NULL);
    if (err != ESP_OK)
      goto release;
  }
  err = rmt_enable(s_rx_channel);
  if (err != ESP_OK)
    goto release;
  s_rx_enabled = true;
  uint8_t command = ST25R3916_CMD_TRANSPARENT_MODE;
  spi_transaction_t transaction = {.length = HB_NFC_SPI_CMD_XFER_BITS, .tx_buffer = &command};
  err = spi_device_polling_transmit(s_spi, &transaction);
  if (err != ESP_OK)
    goto restore;
  transparent = true;
  gpio_set_level(s_mosi, 0);
  esp_rom_gpio_connect_out_signal(s_mosi, SIG_GPIO_OUT_IDX, false, false);
  gpio_set_direction(s_mosi, GPIO_MODE_OUTPUT);
  rmt_receive_config_t receive_config = {.signal_range_min_ns = HB_NFC_RX_GLITCH_NS,
                                         .signal_range_max_ns = HB_NFC_RX_IDLE_NS};
  err = rmt_receive(s_rx_channel, s_rx_symbols, sizeof(s_rx_symbols), &receive_config);
  if (err == ESP_OK) {
    size_t symbols = 0;
    if (xQueueReceive(s_rx_queue, &symbols, pdMS_TO_TICKS(HB_NFC_RX_WINDOW_MS)) != pdTRUE) {
      err = ESP_ERR_TIMEOUT;
    } else if (symbols >= HB_NFC_RX_SYMBOLS) {
      err = ESP_ERR_INVALID_SIZE;
    } else {
      uint32_t time = 0, previous = 0;
      bool has_edge = false, was_high = false;
      for (size_t i = 0; i < symbols && err == ESP_OK; i++) {
        const rmt_symbol_word_t symbol = s_rx_symbols[i];
        uint16_t durations[] = {symbol.duration0, symbol.duration1};
        bool levels[] = {symbol.level0, symbol.level1};
        for (size_t half = 0; half < sizeof(durations) / sizeof(durations[0]); half++) {
          if (durations[half] == 0)
            break;
          if (levels[half] && !was_high &&
              (!has_edge || time - previous >= HB_NFC_RX_REFRACTORY_US)) {
            if (has_edge) {
              if (*out_count == capacity) {
                err = ESP_ERR_INVALID_SIZE;
                break;
              }
              out_gaps[(*out_count)++] = (uint16_t)(time - previous);
            }
            previous = time;
            has_edge = true;
          }
          was_high = levels[half];
          time += durations[half];
        }
      }
    }
  }
restore:
  if (s_rx_enabled) {
    esp_err_t stop_err = rmt_disable(s_rx_channel);
    if (stop_err != ESP_OK)
      err = stop_err;
    else
      s_rx_enabled = false;
  }
  gpio_set_direction(s_mosi, GPIO_MODE_INPUT_OUTPUT);
  esp_rom_gpio_connect_out_signal(s_mosi, spi_periph_signal[s_spi_host].spid_out, false, false);
  if (transparent) {
    command = ST25R3916_CMD_UNMASK_RX_DATA;
    esp_err_t exit_err = spi_device_polling_transmit(s_spi, &transaction);
    if (exit_err != ESP_OK)
      err = exit_err;
  }
release:
  if (s_rx_channel != NULL && !s_rx_enabled) {
    esp_err_t delete_err = rmt_del_channel(s_rx_channel);
    if (delete_err == ESP_OK)
      s_rx_channel = NULL;
    else
      err = delete_err;
  }
  gpio_set_direction(s_miso, GPIO_MODE_INPUT);
  esp_rom_gpio_connect_in_signal(s_miso, spi_periph_signal[s_spi_host].spiq_in, false);
  spi_device_release_bus(s_spi);
unlock:
  if (shared_lock)
    spi_bus_lock_give();
  if (err != ESP_OK)
    *out_count = 0;
  return err;
}

static uint16_t target_ticks(uint32_t cycles, uint32_t *remainder) {
  uint64_t scaled = (uint64_t)cycles * HB_NFC_TX_RESOLUTION_HZ + *remainder;
  *remainder = scaled % HB_NFC_RF_CARRIER_HZ;
  return (uint16_t)(scaled / HB_NFC_RF_CARRIER_HZ);
}

static void encode_target_bit(bool bit, size_t *count, uint32_t *remainder) {
  for (size_t pair = 0; pair < HB_NFC_TX_SYMBOLS_PER_BIT; pair++) {
    bool quiet = bit ? pair == HB_NFC_TX_SYMBOLS_PER_BIT - 1 : pair == 0;
    uint32_t cycles = quiet ? HB_NFC_QUIET_HALF_CYCLES : HB_NFC_SUBCARRIER_HALF_CYCLES;
    rmt_symbol_word_t symbol = {0};
    symbol.level0 = !quiet;
    symbol.duration0 = target_ticks(cycles, remainder);
    symbol.level1 = 0;
    symbol.duration1 = target_ticks(cycles, remainder);
    s_tx_symbols[(*count)++] = symbol;
  }
}

esp_err_t hb_nfc_spi_target_tx_bits(const uint8_t *packed, size_t bits) {
  if (packed == NULL || bits == 0 || bits > HB_NFC_TX_MAX_BITS)
    return ESP_ERR_INVALID_ARG;
  if (s_tx_enabled || s_tx_channel == NULL || !rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ))
    return ESP_ERR_INVALID_STATE;

  size_t count = 0;
  uint32_t remainder = 0;
  encode_target_bit(true, &count, &remainder);
  for (size_t i = 0; i < bits; i++)
    encode_target_bit(
        (packed[i / HB_NFC_BITS_PER_BYTE] >> (i % HB_NFC_BITS_PER_BYTE)) & 1, &count, &remainder);
  s_tx_symbols[count++] = (rmt_symbol_word_t){
      .duration0 = target_ticks(HB_NFC_QUIET_HALF_CYCLES, &remainder),
      .duration1 = target_ticks(HB_NFC_QUIET_HALF_CYCLES, &remainder),
  };

  return transmit_target(count);
}

esp_err_t hb_nfc_spi_target_pulses(const uint16_t *low_cycles, size_t count) {
  if (low_cycles == NULL || count == 0 || count > HB_NFC_TX_MAX_BITS)
    return ESP_ERR_INVALID_ARG;
  if (s_tx_enabled || s_tx_channel == NULL || !rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ))
    return ESP_ERR_INVALID_STATE;
  uint32_t remainder = 0;
  for (size_t i = 0; i < count; i++) {
    if (low_cycles[i] == 0 || low_cycles[i] > HB_NFC_PULSE_MAX_CYCLES)
      return ESP_ERR_INVALID_ARG;
    s_tx_symbols[i] =
        (rmt_symbol_word_t){.level0 = 1,
                            .duration0 = target_ticks(HB_NFC_PULSE_HIGH_CYCLES, &remainder),
                            .level1 = 0,
                            .duration1 = target_ticks(low_cycles[i], &remainder)};
  }
  return transmit_target(count);
}

static size_t encode_v(const void *data,
                       size_t data_size,
                       size_t symbols_written,
                       size_t symbols_free,
                       rmt_symbol_word_t *symbols,
                       bool *done,
                       void *arg) {
  (void)data;
  (void)data_size;
  (void)symbols_written;
  (void)arg;
  size_t count = 0;
  *done = false;
  while (count < symbols_free) {
    bool level;
    uint16_t cycles;
    if (!hb_nfc_v_codec_tx_next(&s_v_tx, &level, &cycles)) {
      *done = true;
      break;
    }
    rmt_symbol_word_t symbol = {0};
    symbol.level0 = level;
    symbol.duration0 = target_ticks(cycles, &s_v_remainder);
    if (hb_nfc_v_codec_tx_next(&s_v_tx, &level, &cycles)) {
      symbol.level1 = level;
      symbol.duration1 = target_ticks(cycles, &s_v_remainder);
    } else {
      /* SOF-only has an odd number of intervals; terminate at inactive level. */
      symbol.duration1 = 1;
      *done = true;
    }
    symbols[count++] = symbol;
    if (*done)
      break;
  }
  return count;
}

esp_err_t hb_nfc_spi_target_v(const uint8_t *frame,
                              size_t length,
                              hb_nfc_v_codec_rate_t rate,
                              bool sof_only) {
  hb_nfc_v_codec_tx_t checked;
  esp_err_t err = hb_nfc_v_codec_tx_init(&checked, frame, length, rate, sof_only);
  if (err != ESP_OK)
    return err;
  if (s_tx_enabled || !s_is_init || !s_tx_channel || !rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ))
    return ESP_ERR_INVALID_STATE;
  if (!s_v_encoder) {
    rmt_simple_encoder_config_t config = {.callback = encode_v, .min_chunk_size = 1};
    err = rmt_new_simple_encoder(&config, &s_v_encoder);
    if (err != ESP_OK)
      return err;
  }
  if (length)
    memcpy(s_v_frame, frame, length);
  s_v_tx = checked;
  s_v_tx.frame = s_v_frame;
  s_v_remainder = 0;
  uint32_t cycles =
      HB_NFC_V_FRAME_OVERHEAD_CYCLES + length * HB_NFC_BITS_PER_BYTE * HB_NFC_V_BIT_CYCLES;
  if (rate == HB_NFC_V_CODEC_RATE_LOW || rate == HB_NFC_V_CODEC_RATE_FAST_LOW)
    cycles *= HB_NFC_V_LOW_RATE_COEFFICIENT;
  int timeout_ms = (int)(((uint64_t)cycles * HB_NFC_MS_PER_SECOND + HB_NFC_RF_CARRIER_HZ - 1) /
                         HB_NFC_RF_CARRIER_HZ) +
                   HB_NFC_TX_TIMEOUT_MS;
  return transmit_target_encoded(s_v_encoder, s_v_frame, length ? length : 1, timeout_ms);
}

esp_err_t hb_nfc_spi_prepare_target_v(gpio_num_t irq_pin) {
  esp_err_t err = hb_nfc_spi_prepare_target_tx();
  if (err != ESP_OK)
    return err;
  if (s_v_encoder == NULL) {
    rmt_simple_encoder_config_t config = {.callback = encode_v, .min_chunk_size = 1};
    err = rmt_new_simple_encoder(&config, &s_v_encoder);
    if (err != ESP_OK)
      return err;
  }
  return hb_nfc_v_rx_init(irq_pin);
}

esp_err_t hb_nfc_spi_target_v_exchange(hb_nfc_spi_v_reply_cb_t reply, void *context) {
  if (!reply)
    return ESP_ERR_INVALID_ARG;
  if (!s_is_init || !s_v_encoder || !s_tx_channel || s_tx_enabled ||
      !rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ))
    return ESP_ERR_INVALID_STATE;
  bool shared_lock = s_spi_host == SPI3_HOST;
  if (shared_lock && !spi_bus_lock_take(HB_NFC_BUS_TIMEOUT_MS))
    return ESP_ERR_TIMEOUT;
  esp_err_t err = spi_device_acquire_bus(s_spi, portMAX_DELAY);
  if (err != ESP_OK)
    goto unlock;
  bool transparent = false;
  uint8_t command = ST25R3916_CMD_TRANSPARENT_MODE;
  spi_transaction_t transaction = {.length = HB_NFC_SPI_CMD_XFER_BITS, .tx_buffer = &command};
  err = spi_device_polling_transmit(s_spi, &transaction);
  if (err != ESP_OK)
    goto release;
  transparent = true;
  if (!s_has_tx_signal) {
    err = rmt_tx_switch_gpio(s_tx_channel, s_mosi, false);
    if (err != ESP_OK)
      goto restore;
    s_tx_signal = GPIO.func_out_sel_cfg[s_mosi].out_sel;
    s_has_tx_signal = true;
  } else {
    esp_rom_gpio_connect_out_signal(s_mosi, s_tx_signal, false, false);
  }
  gpio_set_direction(s_mosi, GPIO_MODE_OUTPUT);
  err = rmt_enable(s_tx_channel);
  if (err != ESP_OK)
    goto restore;
  s_tx_enabled = true;
  size_t rx_length = 0, tx_length = 0;
  bool is_slot = false;
  int64_t eof_us = 0;
  err = hb_nfc_v_rx_capture(s_v_rx_frame, sizeof(s_v_rx_frame), &rx_length, &is_slot, &eof_us);
  if (err != ESP_OK)
    goto restore;
  hb_nfc_v_codec_rate_t rate = HB_NFC_V_CODEC_RATE_HIGH;
  err = reply(
      context, s_v_rx_frame, rx_length, is_slot, s_v_frame, sizeof(s_v_frame), &tx_length, &rate);
  if (err != ESP_OK || tx_length == 0)
    goto restore;
  err = hb_nfc_v_codec_tx_init(&s_v_tx, s_v_frame, tx_length, rate, false);
  if (err != ESP_OK)
    goto restore;
  s_v_remainder = 0;
  int64_t target_us = eof_us + HB_NFC_V_FDT_US;
  int64_t now = esp_timer_get_time();
  if (now > target_us + HB_NFC_V_LATE_US) {
    err = ESP_ERR_TIMEOUT;
    goto restore;
  }
  if (now < target_us)
    esp_rom_delay_us((uint32_t)(target_us - now));
  rmt_transmit_config_t config = {.loop_count = 0, .flags.eot_level = 0};
  err = rmt_transmit(s_tx_channel, s_v_encoder, s_v_frame, tx_length, &config);
  if (err == ESP_OK) {
    uint32_t cycles =
        HB_NFC_V_FRAME_OVERHEAD_CYCLES + tx_length * HB_NFC_BITS_PER_BYTE * HB_NFC_V_BIT_CYCLES;
    if (rate == HB_NFC_V_CODEC_RATE_LOW || rate == HB_NFC_V_CODEC_RATE_FAST_LOW)
      cycles *= HB_NFC_V_LOW_RATE_COEFFICIENT;
    int timeout_ms = (int)(((uint64_t)cycles * HB_NFC_MS_PER_SECOND + HB_NFC_RF_CARRIER_HZ - 1) /
                           HB_NFC_RF_CARRIER_HZ) +
                     HB_NFC_TX_TIMEOUT_MS;
    err = rmt_tx_wait_all_done(s_tx_channel, timeout_ms);
  }
restore:
  if (s_tx_enabled) {
    esp_err_t stop_err = rmt_disable(s_tx_channel);
    if (stop_err == ESP_OK)
      s_tx_enabled = false;
    else
      err = stop_err;
  }
  gpio_set_direction(s_mosi, GPIO_MODE_INPUT_OUTPUT);
  esp_rom_gpio_connect_out_signal(s_mosi, spi_periph_signal[s_spi_host].spid_out, false, false);
  if (transparent) {
    command = ST25R3916_CMD_UNMASK_RX_DATA;
    esp_err_t exit_err = spi_device_polling_transmit(s_spi, &transaction);
    if (exit_err != ESP_OK)
      err = exit_err;
  }
release:
  spi_device_release_bus(s_spi);
unlock:
  if (shared_lock)
    spi_bus_lock_give();
  return err;
}

static esp_err_t transmit_target(size_t count) {
  return transmit_target_encoded(
      s_tx_encoder, s_tx_symbols, count * sizeof(s_tx_symbols[0]), HB_NFC_TX_TIMEOUT_MS);
}

static esp_err_t transmit_target_encoded(rmt_encoder_handle_t encoder,
                                         const void *data,
                                         size_t size,
                                         int timeout_ms) {
  bool shared_lock = s_spi_host == SPI3_HOST;
  if (shared_lock && !spi_bus_lock_take(HB_NFC_BUS_TIMEOUT_MS))
    return ESP_ERR_TIMEOUT;
  esp_err_t err = spi_device_acquire_bus(s_spi, portMAX_DELAY);
  if (err != ESP_OK)
    goto unlock;

  bool enabled = false;
  bool transparent = false;
  uint8_t command = ST25R3916_CMD_TRANSPARENT_MODE;
  spi_transaction_t transaction = {.length = HB_NFC_SPI_CMD_XFER_BITS, .tx_buffer = &command};
  err = spi_device_polling_transmit(s_spi, &transaction);
  if (err != ESP_OK)
    goto release;
  transparent = true;

  if (!s_has_tx_signal) {
    err = rmt_tx_switch_gpio(s_tx_channel, s_mosi, false);
    if (err != ESP_OK)
      goto restore;
    s_tx_signal = GPIO.func_out_sel_cfg[s_mosi].out_sel;
    s_has_tx_signal = true;
  } else {
    esp_rom_gpio_connect_out_signal(s_mosi, s_tx_signal, false, false);
  }
  gpio_set_direction(s_mosi, GPIO_MODE_OUTPUT);
  err = rmt_enable(s_tx_channel);
  if (err != ESP_OK)
    goto restore;
  enabled = true;
  s_tx_enabled = true;
  rmt_transmit_config_t tx_config = {.loop_count = 0, .flags.eot_level = 0};
  err = rmt_transmit(s_tx_channel, encoder, data, size, &tx_config);
  if (err == ESP_OK)
    err = rmt_tx_wait_all_done(s_tx_channel, timeout_ms);

restore:
  if (enabled) {
    esp_err_t stop_err = rmt_disable(s_tx_channel);
    if (stop_err == ESP_OK)
      s_tx_enabled = false;
    if (err == ESP_OK)
      err = stop_err;
  }
  gpio_set_direction(s_mosi, GPIO_MODE_INPUT_OUTPUT);
  esp_rom_gpio_connect_out_signal(s_mosi, spi_periph_signal[s_spi_host].spid_out, false, false);
  if (transparent) {
    command = ST25R3916_CMD_UNMASK_RX_DATA;
    esp_err_t exit_err = spi_device_polling_transmit(s_spi, &transaction);
    if (err == ESP_OK)
      err = exit_err;
  }
release:
  spi_device_release_bus(s_spi);
unlock:
  if (shared_lock)
    spi_bus_lock_give();
  return err;
}

static esp_err_t transmit(spi_transaction_t *t) {
  if (!rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ))
    return ESP_ERR_INVALID_STATE;
  if (s_spi == NULL || t == NULL)
    return ESP_FAIL;

  const bool shared_lock = s_spi_host == SPI3_HOST;
  bool locked = !shared_lock || spi_bus_lock_take(100);
  if (!locked) {
    ESP_LOGW(TAG, "shared SPI bus lock timeout");
    return ESP_ERR_TIMEOUT;
  }

  if (!rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ)) {
    if (shared_lock)
      spi_bus_lock_give();
    return ESP_ERR_INVALID_STATE;
  }
  /* Authentication replies must not wait for a task wake-up on every register. */
  esp_err_t ret = spi_device_polling_transmit(s_spi, t);
  if (shared_lock) {
    spi_bus_lock_give();
  }
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "spi transmit fail: %s", esp_err_to_name(ret));
    return ESP_FAIL;
  }

  return ESP_OK;
}
