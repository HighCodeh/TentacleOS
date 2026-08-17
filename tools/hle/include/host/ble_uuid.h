#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int ble_uuid_any_t;
typedef int ble_uuid16_t;
#define BLE_UUID16_DECLARE(x) ((x) & 0xFFFF)
#define BLE_UUID16(x) ((x) & 0xFFFF)

#ifdef __cplusplus
}
#endif
