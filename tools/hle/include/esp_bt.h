#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_BT_MODE_BTDM 0
#define ESP_BT_MODE_BLE  1
void esp_bt_controller_mem_release(int mode);
esp_err_t esp_bt_controller_enable(int mode);
esp_err_t esp_bt_controller_disable(void);
esp_err_t esp_bt_controller_init(void *cfg);
void esp_bt_controller_deinit(void);

#ifdef __cplusplus
}
#endif
