#pragma once

/* Minimal ESP-IDF 5.5.3-compatible RMT type declarations for HLE build.
 * Behavioral semantics are intentionally incomplete — do not use for ABI interop.
 * Layout-sensitive types mirror upstream bitfield patterns and are guarded by
 * static assertions where data integrity matters for protocol encode/decode. */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef union {
    struct {
        uint16_t duration0 : 15;
        uint16_t level0    : 1;
        uint16_t duration1 : 15;
        uint16_t level1    : 1;
    };
    uint32_t val;
} rmt_symbol_word_t;

#ifdef __cplusplus
static_assert(sizeof(rmt_symbol_word_t) == sizeof(uint32_t),
              "rmt_symbol_word_t must be 4 bytes");
#else
_Static_assert(sizeof(rmt_symbol_word_t) == sizeof(uint32_t),
               "rmt_symbol_word_t must be 4 bytes");
#endif

typedef void *rmt_channel_handle_t;
typedef void *rmt_encoder_handle_t;

typedef struct {
    struct {
        uint32_t invert_in : 1;
        uint32_t with_dma : 1;
        uint32_t io_loop_back : 1;
    } flags;
    uint32_t resolution_hz;
    int gpio_num;
    int clk_src;
    int mem_block_symbols;
} rmt_rx_channel_config_t;

typedef struct {
    struct {
        uint32_t invert_out : 1;
        uint32_t with_dma : 1;
        uint32_t io_loop_back : 1;
        uint32_t io_od_mode : 1;
    } flags;
    uint32_t resolution_hz;
    int gpio_num;
    int clk_src;
    int mem_block_symbols;
    int trans_queue_depth;
} rmt_tx_channel_config_t;

typedef struct {
    rmt_symbol_word_t *buffer;
    size_t num_symbols;
    uint32_t signal_range_min_ns;
    uint32_t signal_range_max_ns;
} rmt_receive_config_t;

typedef struct {
    struct {
        uint32_t invert_out : 1;
    } flags;
} rmt_copy_encoder_config_t;

typedef struct {
    rmt_channel_handle_t channel;
    rmt_symbol_word_t *received_symbols;
    size_t num_received_symbols;
    size_t num_symbols;
} rmt_rx_done_event_data_t;

typedef bool (*rmt_rx_event_callback_t)(rmt_channel_handle_t channel,
                                         const rmt_rx_done_event_data_t *edata,
                                         void *user_ctx);

typedef struct {
    rmt_rx_event_callback_t on_recv_done;
    void *user_ctx;
} rmt_rx_event_callbacks_t;

typedef struct {
    uint32_t loop_count;
    uint32_t flags;
} rmt_transmit_config_t;

typedef struct {
    struct {
        uint32_t polarity_active_low : 1;
    } flags;
    uint32_t duty_cycle;
    uint32_t frequency_hz;
} rmt_carrier_config_t;

#ifdef __cplusplus
}
#endif
