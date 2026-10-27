#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#define BLE_GAP_EVENT_CONNECT 0
#define BLE_GAP_EVENT_DISCONNECT 1
struct ble_gap_event {
 int type;
 struct {int status;uint16_t conn_handle;} connect;
 struct {struct {uint16_t conn_handle;} conn;} disconnect;
};
bool bluetooth_service_is_running(void);
esp_err_t bluetooth_service_connect(const uint8_t *,uint8_t,int (*)(struct ble_gap_event *,void *));
int ble_gap_terminate(uint16_t,uint8_t);
int ble_gap_conn_cancel(void);
