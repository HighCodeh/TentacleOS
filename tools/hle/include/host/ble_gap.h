#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int ble_gap_event;
typedef int ble_gap_conn_desc;

#define BLE_OWN_ADDR_RANDOM      1
#define BLE_OWN_ADDR_PUBLIC      0
#define BLE_HS_ADV_F_DISC_GEN    0x01
#define BLE_HS_ADV_F_BREDR_UNSUP 0x20
#define BLE_HS_ADV_TX_PWR_LVL_AUTO 127
#define BLE_ERR_REM_USER_CONN_TERM 0x13
#define BLE_GAP_INITIAL_CONN_ITVL_MIN 6
#define BLE_GAP_INITIAL_CONN_ITVL_MAX 12

typedef struct {
    int dummy;
} ble_hs_adv_fields;

typedef int ble_store_util_status_rr_t;

void ble_store_util_status_rr(const void *peer, void *out, int *num);

#ifdef __cplusplus
}
#endif
