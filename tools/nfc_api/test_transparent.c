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

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hb_nfc_v_codec.h"
#define ESP_OK 0
#define ESP_LOGE(...) ((void)0)
#define RMT_CLK_SRC_DEFAULT 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_TIMEOUT 3
#define RF_FRONTEND_NFC_13MHZ 0
#define SPI3_HOST 0
#define portMAX_DELAY 0
#define GPIO_MODE_INPUT 2
#define SIG_GPIO_OUT_IDX 99
#define GPIO_MODE_OUTPUT 0
#define GPIO_MODE_INPUT_OUTPUT 1
#define ST25R3916_CMD_TRANSPARENT_MODE 0xdc
#define ST25R3916_CMD_UNMASK_RX_DATA 0x16
/* CONSTANTS */
typedef int esp_err_t;
typedef void *rmt_encoder_handle_t;
typedef struct { uint16_t duration0, duration1; unsigned level0, level1; } rmt_symbol_word_t;
typedef struct { unsigned length; const void *tx_buffer; } spi_transaction_t;
typedef struct { int loop_count; struct { unsigned eot_level; } flags; } rmt_transmit_config_t;
static rmt_symbol_word_t s_tx_symbols[(HB_NFC_TX_MAX_BITS + 2) * HB_NFC_TX_SYMBOLS_PER_BIT];
static bool s_tx_enabled;
static void *s_tx_channel = (void *)1;
static void *s_tx_encoder = (void *)1;
static void *s_v_encoder;
static hb_nfc_v_codec_tx_t s_v_tx;
static uint8_t s_v_frame[HB_NFC_V_CODEC_MAX_FRAME];
static uint8_t s_v_rx_frame[HB_NFC_V_CODEC_MAX_FRAME];
static uint32_t s_v_remainder;
static uint64_t s_v_ticks;
static int s_timeout_ms;
static size_t encode_v(const void *, size_t, size_t, size_t, rmt_symbol_word_t *, bool *, void *);
typedef struct {
  size_t (*callback)(const void *, size_t, size_t, size_t, rmt_symbol_word_t *, bool *, void *);
  size_t min_chunk_size;
} rmt_simple_encoder_config_t;
static int s_spi = 1, s_spi_host = SPI3_HOST, s_mosi = 0, s_sclk = 1;
static uint32_t s_tx_signal;
static bool s_has_tx_signal;
static bool s_is_tx_ready = true;
static struct { union { uint32_t out_sel; uint32_t val; } func_out_sel_cfg[2]; } GPIO;
typedef int gpio_num_t;
typedef struct { bool oe; unsigned fun_sel; } gpio_io_config_t;
#define PIN_FUNC_GPIO 1
static unsigned s_pad_function[2] = {2, 2};
static bool s_pad_output[2] = {true, true};
static bool s_clock_high;
static int gpio_set_level(int gpio, int level);
static int gpio_get_io_config(int pin, gpio_io_config_t *out) {
  out->oe = s_pad_output[pin]; out->fun_sel = s_pad_function[pin]; return ESP_OK;
}
static void esp_gpio_reserve(uint64_t pins) { assert(pins == 1 || pins == 2); }
static int gpio_output_enable(int pin) {
  s_pad_output[pin] = true; GPIO.func_out_sel_cfg[pin].val = SIG_GPIO_OUT_IDX; return ESP_OK;
}
static int gpio_output_disable(int pin) { s_pad_output[pin] = false; return ESP_OK; }
static int gpio_func_sel(int pin, unsigned fn) { s_pad_function[pin] = fn; return ESP_OK; }
static const struct { uint32_t spid_out, spiq_in; } spi_periph_signal[1] = {{12, 13}};
static unsigned s_operation, s_fail_at, s_count;
static bool s_lock, s_bus, s_enabled, s_transparent, s_disable_fail;
static int operation(void) { return ++s_operation == s_fail_at ? 99 : ESP_OK; }
static bool rf_frontend_can_access(int owner) { (void)owner; return true; }
static bool spi_bus_lock_take(int ms) { (void)ms; assert(!s_lock); s_lock = operation() == 0; return s_lock; }
static void spi_bus_lock_give(void) { assert(s_lock); s_lock = false; }
static int spi_device_acquire_bus(int dev, int wait) {
  (void)dev; (void)wait; assert(s_lock && !s_bus);
  int err = operation(); s_bus = err == 0; return err;
}
static void spi_device_release_bus(int dev) { (void)dev; assert(s_bus); s_bus = false; }
static int spi_device_polling_transmit(int dev, spi_transaction_t *t) {
  (void)dev; assert(s_bus && s_lock); assert(t->length == 8);
  uint8_t cmd = *(const uint8_t *)t->tx_buffer;
  int err = operation();
  if (cmd == ST25R3916_CMD_TRANSPARENT_MODE) {
    if (!err) s_transparent = true;
  } else {
    assert(cmd == ST25R3916_CMD_UNMASK_RX_DATA);
    assert(GPIO.func_out_sel_cfg[0].out_sel == 12 && (!s_enabled || s_disable_fail));
    s_transparent = false;
  }
  return err;
}
static int rmt_tx_switch_gpio(void *channel, int pin, bool invert) {
  (void)channel; (void)pin; (void)invert; assert(s_bus && s_transparent);
  int err = operation(); if (!err) GPIO.func_out_sel_cfg[0].out_sel = 42; return err;
}
static void esp_rom_gpio_connect_out_signal(int pin, uint32_t signal, bool inv, bool oe) {
  (void)pin; (void)inv; (void)oe; assert(s_bus); GPIO.func_out_sel_cfg[0].out_sel = signal;
}
static int gpio_set_direction(int pin, int mode) { (void)pin; (void)mode; return 0; }
static int rmt_enable(void *ch) { (void)ch; int err = operation(); s_enabled = err == 0; return err; }
static int rmt_disable(void *ch) { (void)ch; assert(s_enabled);
  if (s_disable_fail) return 99;
  s_enabled = false; return 0;
}
static int rmt_transmit(void *ch, void *enc, const void *symbols, size_t bytes, const rmt_transmit_config_t *cfg) {
  (void)ch; (void)enc; assert(s_enabled && s_bus && s_transparent);
  assert(cfg->flags.eot_level == 0);
  int err = operation();
  if (err) return err;
  if (enc == (void *)2) {
    bool done = false;
    size_t written = 0;
    while (!done) {
      rmt_symbol_word_t buffer[31];
      size_t capacity = written % 31 + 1;
      size_t count = encode_v(symbols, bytes, written, capacity, buffer, &done, NULL);
      assert(count <= capacity && (count || done));
      for (size_t i = 0; i < count; i++) {
        assert(buffer[i].duration0 && buffer[i].duration1);
        assert(buffer[i].duration0 < 32768 && buffer[i].duration1 < 32768);
        s_v_ticks += buffer[i].duration0 + buffer[i].duration1;
      }
      written += count;
    }
  } else {
    assert(symbols == s_tx_symbols);
    s_count = bytes / sizeof(rmt_symbol_word_t);
  }
  return ESP_OK;
}
static int rmt_tx_wait_all_done(void *ch, int ms) { (void)ch; assert(ms > 0); s_timeout_ms = ms; return operation(); }
typedef struct { int unused; } rmt_copy_encoder_config_t;
typedef struct { int gpio_num, clk_src, resolution_hz, mem_block_symbols, trans_queue_depth;
  struct { bool with_dma; } flags; } rmt_tx_channel_config_t;
static bool s_is_init = true;
static int rmt_new_copy_encoder(const rmt_copy_encoder_config_t *config, void **encoder) {
  (void)config; int err = operation();
  if (!err) *encoder = (void *)1;
  return err;
}
static int rmt_new_simple_encoder(const rmt_simple_encoder_config_t *config, void **encoder) {
  assert(config->callback == encode_v && config->min_chunk_size == 1);
  int err = operation();
  if (!err) *encoder = (void *)2;
  return err;
}
static int rmt_del_encoder(void *encoder) { (void)encoder; return ESP_OK; }
static int rmt_new_tx_channel(const rmt_tx_channel_config_t *config, void **channel) {
  assert(config->gpio_num == s_mosi && config->gpio_num >= 0);
  assert(config->flags.with_dma);
  assert((size_t)config->mem_block_symbols >= (HB_NFC_TX_MAX_BITS + 1) * HB_NFC_TX_SYMBOLS_PER_BIT + 2);
  assert((config->mem_block_symbols * 4U) % 64 == 0);
  assert(config->mem_block_symbols * 4U < 4096);
  int err = operation();
  if (err) return err;
  GPIO.func_out_sel_cfg[s_mosi].out_sel = 77;
  *channel = (void *)1; return ESP_OK;
}
#define ESP_ERR_INVALID_SIZE 4
#define pdFALSE 0
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
typedef int BaseType_t;
typedef int StaticQueue_t;
typedef void *QueueHandle_t;
typedef void *rmt_channel_handle_t;
typedef struct { size_t num_symbols; } rmt_rx_done_event_data_t;
typedef struct { int gpio_num,clk_src,resolution_hz,mem_block_symbols;
  struct { bool with_dma; } flags; } rmt_rx_channel_config_t;
typedef struct { bool (*on_recv_done)(rmt_channel_handle_t,const rmt_rx_done_event_data_t *,void *); } rmt_rx_event_callbacks_t;
typedef struct { uint32_t signal_range_min_ns,signal_range_max_ns; } rmt_receive_config_t;
static rmt_channel_handle_t s_rx_channel;
static bool s_rx_enabled;
static rmt_symbol_word_t s_rx_symbols[HB_NFC_RX_SYMBOLS];
static StaticQueue_t s_rx_queue_state;
static uint8_t s_rx_queue_storage[sizeof(size_t)];
static QueueHandle_t s_rx_queue;
static int s_miso;
static int xQueueSendFromISR(QueueHandle_t q,const size_t *count,BaseType_t *wake) {
  (void)q;(void)count;*wake=0;return 1;
}
static QueueHandle_t xQueueCreateStatic(unsigned depth,size_t size,void *storage,void *state) {
  (void)depth;(void)size;(void)storage;(void)state;return (void *)1;
}
static void xQueueReset(QueueHandle_t q) { (void)q; }
static int xQueueReceive(QueueHandle_t q,size_t *symbols,int timeout) {
  (void)q;(void)timeout;*symbols=128;return operation()==0;
}
static int rmt_new_rx_channel(const rmt_rx_channel_config_t *config,void **channel) {
  assert(config->gpio_num==s_miso);int err=operation();
  if(!err)*channel=(void *)1;return err;
}
static int rmt_rx_register_event_callbacks(void *channel,const rmt_rx_event_callbacks_t *callbacks,void *ctx) {
  (void)channel;(void)callbacks;(void)ctx;return operation();
}
static int rmt_receive(void *channel,void *buffer,size_t bytes,const rmt_receive_config_t *config) {
  (void)channel;assert(s_enabled && s_bus && s_transparent);
  assert(bytes==sizeof(s_rx_symbols) && buffer==s_rx_symbols);
  assert(config->signal_range_max_ns==200000);
  for(unsigned i=0;i<128;i++) s_rx_symbols[i]=(rmt_symbol_word_t){.duration0=5,.duration1=35,.level0=1,.level1=0};
  return operation();
}
static int rmt_del_channel(void *channel) { (void)channel;assert(!s_enabled);return 0; }
static int gpio_set_level(int gpio,int level) {
  if (gpio == s_sclk) { assert(level == 1); s_clock_high = true; }
  else assert(level == 0);
  return ESP_OK;
}
static void esp_rom_gpio_connect_in_signal(int gpio,uint32_t signal,bool invert) {
  (void)gpio;(void)signal;(void)invert;assert(s_bus);
}
typedef esp_err_t (*hb_nfc_spi_v_reply_cb_t)(void *, const uint8_t *, size_t, bool,
    uint8_t *, size_t, size_t *, hb_nfc_v_codec_rate_t *);
static int64_t s_now;
static bool s_silent, s_late;
static int64_t esp_timer_get_time(void) { return s_now; }
static void esp_rom_delay_us(uint32_t us) { s_now += us; }
static esp_err_t hb_nfc_v_rx_capture(uint8_t *data, size_t cap, size_t *length,
                                      bool *slot, int64_t *eof) {
  assert(s_bus && s_lock && s_transparent && s_enabled);
  assert(cap == HB_NFC_V_CODEC_MAX_FRAME);
  int err = operation();
  if(err) return err;
  data[0] = 0x26; *length = 1; *slot = false; *eof = 1000;
  s_now = s_late ? 2000 : 1100;
  return ESP_OK;
}
static esp_err_t v_reply(void *ctx,const uint8_t *data,size_t length,bool slot,
                         uint8_t *out,size_t cap,size_t *count,hb_nfc_v_codec_rate_t *rate) {
  (void)ctx;
  assert(s_bus && s_transparent && !slot && data[0]==0x26 && length==1 && cap>=4);
  out[0]=0;out[1]=0x87;out[2]=0x0f;*count=s_silent?0:3;
  *rate=HB_NFC_V_CODEC_RATE_HIGH;
  return operation();
}
/* PRODUCTION */

static void reset(void) {
  s_tx_enabled = false; s_disable_fail = false;
  s_operation = 0; s_fail_at = 0; s_count = 0; s_v_ticks = 0; s_timeout_ms = 0;
  s_lock = s_bus = s_enabled = s_transparent = s_has_tx_signal = false;
  GPIO.func_out_sel_cfg[0].out_sel = 12;
}
int main(void) {
  reset(); s_v_encoder=(void *)2;
  assert(hb_nfc_spi_target_v_exchange(v_reply,NULL)==ESP_OK);
  assert(s_now==1319 && !s_bus && !s_lock && !s_enabled && !s_transparent);
  unsigned v_operations=s_operation;
  for(unsigned fault=1;fault<=v_operations;fault++) {
    reset();s_fail_at=fault;
    assert(hb_nfc_spi_target_v_exchange(v_reply,NULL)!=ESP_OK);
    assert(!s_bus && !s_lock && !s_enabled);
  }
  reset();s_silent=true;
  assert(hb_nfc_spi_target_v_exchange(v_reply,NULL)==ESP_OK && s_v_ticks==0);
  s_silent=false;reset();s_late=true;
  assert(hb_nfc_spi_target_v_exchange(v_reply,NULL)==ESP_ERR_TIMEOUT && s_v_ticks==0);
  s_late=false;reset();s_disable_fail=true;
  assert(hb_nfc_spi_target_v_exchange(v_reply,NULL)!=ESP_OK && s_tx_enabled);
  assert(hb_nfc_spi_target_v_exchange(v_reply,NULL)==ESP_ERR_INVALID_STATE);

  uint16_t gaps[127];size_t gap_count=0;
  reset();
  assert(hb_nfc_spi_capture_gaps(gaps,127,&gap_count)==ESP_OK);
  assert(gap_count==127 && !s_lock && !s_bus && !s_rx_enabled && !s_rx_channel);
  for(size_t i=0;i<gap_count;i++) assert(gaps[i]==40);
  unsigned rx_operations=s_operation;
  for(unsigned fail=1;fail<=rx_operations;fail++) {
    reset();s_fail_at=fail;
    assert(hb_nfc_spi_capture_gaps(gaps,127,&gap_count)!=ESP_OK);
    assert(gap_count==0 && !s_lock && !s_bus && !s_rx_enabled && !s_rx_channel);
  }
  reset();

  s_tx_channel = NULL; s_tx_encoder = NULL;
  assert(hb_nfc_spi_prepare_target_tx() == ESP_OK);
  assert(s_has_tx_signal && s_tx_signal == 77);
  assert(GPIO.func_out_sel_cfg[s_mosi].out_sel == spi_periph_signal[s_spi_host].spid_out);
  unsigned allocation_operations = s_operation;
  for (unsigned fail = 1; fail <= allocation_operations; fail++) {
    reset(); s_tx_channel = NULL; s_tx_encoder = NULL; s_fail_at = fail;
    assert(hb_nfc_spi_prepare_target_tx() != ESP_OK);
    assert(!s_lock && !s_bus && s_tx_channel == NULL && s_tx_encoder == NULL);
    assert(GPIO.func_out_sel_cfg[s_mosi].out_sel == spi_periph_signal[s_spi_host].spid_out);
  }
  reset();
  assert(hb_nfc_spi_prepare_target_tx() == ESP_OK);
  uint8_t data[21]; memset(data, 0xa5, sizeof(data));
  for (size_t bits = 1; bits <= HB_NFC_TX_MAX_BITS; bits++) {
    reset();
    assert(hb_nfc_spi_target_tx_bits(data, bits) == ESP_OK);
    assert(s_count == (bits + 1) * 5 + 1);
    uint64_t ticks = 0;
    for (size_t b = 0; b <= bits; b++) {
      bool bit = b == 0 || ((data[(b - 1) / 8] >> ((b - 1) % 8)) & 1);
      for (size_t pair = 0; pair < 5; pair++) {
        rmt_symbol_word_t symbol = s_tx_symbols[b * 5 + pair];
        bool quiet = bit ? pair == 4 : pair == 0;
        assert(symbol.level0 == !quiet && symbol.level1 == 0);
        assert(symbol.duration0 > 0 && symbol.duration1 > 0);
        ticks += symbol.duration0 + symbol.duration1;
      }
    }
    ticks += s_tx_symbols[s_count - 1].duration0 + s_tx_symbols[s_count - 1].duration1;
    assert(ticks == ((uint64_t)(bits + 1) * 128 + 64) * HB_NFC_TX_RESOLUTION_HZ / HB_NFC_RF_CARRIER_HZ);
    assert(!s_lock && !s_bus && !s_enabled && !s_transparent);
    assert(GPIO.func_out_sel_cfg[0].out_sel == 12);
    assert(s_pad_function[s_mosi] == 2 && s_pad_function[s_sclk] == 2);
    assert(s_clock_high);
  }
  unsigned operations = s_operation;
  for (unsigned fail = 1; fail <= operations; fail++) {
    reset(); s_fail_at = fail;
    assert(hb_nfc_spi_target_tx_bits(data, 36) != ESP_OK);
    assert(!s_lock && !s_bus && !s_enabled && !s_transparent);
    assert(GPIO.func_out_sel_cfg[0].out_sel == 12);
  }
  reset();
  const uint16_t lows[] = {448, 1088, 1600};
  assert(hb_nfc_spi_target_pulses(lows, 3) == ESP_OK);
  assert(s_count == 3);
  for (size_t i = 0; i < 3; i++) {
    assert(s_tx_symbols[i].level0 == 1 && s_tx_symbols[i].level1 == 0);
    assert(s_tx_symbols[i].duration0 > 0 && s_tx_symbols[i].duration1 > 0);
  }
  assert(!s_lock && !s_bus && !s_enabled && !s_transparent);
  const uint16_t invalid[] = {0};
  assert(hb_nfc_spi_target_pulses(invalid, 1) == ESP_ERR_INVALID_ARG);
  uint8_t v_frame[1024]; memset(v_frame, 0x95, sizeof(v_frame));
  for (unsigned rate = 0; rate < 2; rate++) {
    reset();
    assert(hb_nfc_spi_target_v(v_frame, sizeof(v_frame), rate, false) == ESP_OK);
    uint64_t cycles = (2560ULL + sizeof(v_frame) * 8 * 512) * (rate ? 4 : 1);
    assert(s_v_ticks == cycles * HB_NFC_TX_RESOLUTION_HZ / HB_NFC_RF_CARRIER_HZ);
    assert((uint64_t)s_timeout_ms * HB_NFC_RF_CARRIER_HZ > cycles * 1000);
    assert(!s_lock && !s_bus && !s_enabled && !s_transparent);
    reset();
    assert(hb_nfc_spi_target_v(NULL, 0, rate, true) == ESP_OK);
    assert(s_v_ticks == 1280ULL * (rate ? 4 : 1) * HB_NFC_TX_RESOLUTION_HZ / HB_NFC_RF_CARRIER_HZ + 1);
  }
  reset(); s_v_encoder = NULL;
  assert(hb_nfc_spi_target_v(v_frame, 16, 0, false) == ESP_OK);
  operations = s_operation;
  for (unsigned fail = 1; fail <= operations; fail++) {
    reset(); s_v_encoder = NULL; s_fail_at = fail;
    assert(hb_nfc_spi_target_v(v_frame, 16, 0, false) != ESP_OK);
    assert(!s_lock && !s_bus && !s_enabled && !s_transparent);
    assert(GPIO.func_out_sel_cfg[0].out_sel == 12);
  }
  reset(); s_disable_fail = true;
  assert(hb_nfc_spi_target_v(v_frame, 16, 0, false) != ESP_OK);
  assert(s_tx_enabled && s_enabled);
  unsigned previous_operations = s_operation;
  uint8_t prior_frame[16]; memcpy(prior_frame, s_v_frame, sizeof(prior_frame));
  v_frame[0] ^= 0xff;
  assert(hb_nfc_spi_target_v(v_frame, 16, 0, false) == ESP_ERR_INVALID_STATE);
  assert(hb_nfc_spi_target_tx_bits(data, 36) == ESP_ERR_INVALID_STATE);
  assert(hb_nfc_spi_target_pulses(lows, 3) == ESP_ERR_INVALID_STATE);
  assert(s_operation == previous_operations && !memcmp(prior_frame, s_v_frame, sizeof(prior_frame)));
  s_disable_fail = false; assert(rmt_disable(s_tx_channel) == ESP_OK); s_tx_enabled = false;
  puts("PASS: Classic/Texkom/NFC-V waveforms, streamed timing, SPI cleanup and failed-stop retention");
}
