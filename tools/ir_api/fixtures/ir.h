#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef enum {IR_PROTO_UNKNOWN=0,IR_PROTO_NEC,IR_PROTO_SAMSUNG,IR_PROTO_RC6,IR_PROTO_RC5,IR_PROTO_SONY,IR_PROTO_LG,IR_PROTO_JVC,IR_PROTO_DENON,IR_PROTO_PANASONIC,IR_PROTO_RCA,IR_PROTO_PIONEER,IR_PROTO_NEC42,IR_PROTO_COUNT} ir_protocol_t;
typedef struct {ir_protocol_t protocol;uint32_t address,command;bool repeat,extended;uint8_t nbits;} ir_data_t;
typedef uint32_t rmt_symbol_word_t;
esp_err_t ir_rx_init(void);
void ir_rx_prime(void);
void ir_rx_deinit(void);
esp_err_t ir_tx_init(void);
void ir_tx_deinit(void);
esp_err_t ir_flash_on(void);
esp_err_t ir_flash_off(void);
esp_err_t ir_receive(ir_data_t *,uint32_t);
esp_err_t ir_send(const ir_data_t *);
esp_err_t ir_send_raw(const rmt_symbol_word_t *,size_t,uint32_t);
esp_err_t ir_get_last_raw(rmt_symbol_word_t *,size_t,size_t *);
