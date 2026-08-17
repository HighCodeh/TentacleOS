#pragma once

/* Minimal ESP-IDF 5.5.3-compatible RMT API declarations for HLE build.
 * Behavioral semantics are intentionally incomplete — do not use for ABI interop. */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "driver/rmt_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RMT_CLK_SRC_DEFAULT 0

esp_err_t rmt_new_rx_channel(const rmt_rx_channel_config_t *config, rmt_channel_handle_t *ret_chan);
esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t *config, rmt_channel_handle_t *ret_chan);
esp_err_t rmt_new_copy_encoder(const rmt_copy_encoder_config_t *config, rmt_encoder_handle_t *ret_encoder);
esp_err_t rmt_rx_register_event_callbacks(rmt_channel_handle_t rx_chan, const rmt_rx_event_callbacks_t *cbs, void *user_data);
esp_err_t rmt_enable(rmt_channel_handle_t chan);
esp_err_t rmt_disable(rmt_channel_handle_t chan);
esp_err_t rmt_receive(rmt_channel_handle_t rx_chan, rmt_symbol_word_t *buffer, size_t buffer_size, const rmt_receive_config_t *config);
esp_err_t rmt_transmit(rmt_channel_handle_t tx_chan, rmt_encoder_handle_t encoder, const void *payload, size_t payload_bytes, const rmt_transmit_config_t *config);
esp_err_t rmt_tx_wait_all_done(rmt_channel_handle_t tx_chan, int timeout_ms);
esp_err_t rmt_del_channel(rmt_channel_handle_t chan);
esp_err_t rmt_del_encoder(rmt_encoder_handle_t encoder);
esp_err_t rmt_apply_carrier(rmt_channel_handle_t tx_chan, const rmt_carrier_config_t *config);

#ifdef __cplusplus
}
#endif
