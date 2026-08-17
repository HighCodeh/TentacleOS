#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int ble_addr_t;
#define BLE_ADDR_PUBLIC 0
#define BLE_ADDR_RANDOM 1
#define BLE_ADDR_TYPE_PUBLIC 0
#define BLE_ADDR_TYPE_RANDOM 1

typedef struct {
    uint8_t val[6];
    uint8_t type;
} ble_addr_le_t;

#ifdef __cplusplus
}
#endif
