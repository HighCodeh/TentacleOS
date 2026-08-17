#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int uart_port_t;
#define UART_NUM_0 0
#define UART_NUM_1 1
#define UART_NUM_2 2

typedef struct { int baud_rate; int data_bits; int parity; int stop_bits; } uart_config_t;

#ifdef __cplusplus
}
#endif
