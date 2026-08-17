#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int i2c_port_t;
#define I2C_NUM_0 0

typedef int i2c_cmd_handle_t;

typedef struct {
    int mode;
    int sda_io_num;
    int scl_io_num;
    bool sda_pullup_en;
    bool scl_pullup_en;
    struct {
        uint32_t clk_speed;
    } master;
} i2c_config_t;

#define I2C_MODE_MASTER 1
#define I2C_MODE_SLAVE  2
#define ACK_CHECK_EN    1
#define I2C_MASTER_WRITE 0
#define I2C_MASTER_READ  1
#define I2C_MASTER_LAST_NACK 1

esp_err_t i2c_param_config(i2c_port_t port, const i2c_config_t *cfg);
esp_err_t i2c_driver_install(i2c_port_t port, int mode, int rx_buf, int tx_buf, int flags);
i2c_cmd_handle_t i2c_cmd_link_create(void);
esp_err_t i2c_master_start(i2c_cmd_handle_t cmd);
esp_err_t i2c_master_write_byte(i2c_cmd_handle_t cmd, uint8_t data, bool ack_en);
esp_err_t i2c_master_read_byte(i2c_cmd_handle_t cmd, uint8_t *data, int ack);
esp_err_t i2c_master_stop(i2c_cmd_handle_t cmd);
esp_err_t i2c_master_cmd_begin(i2c_port_t port, i2c_cmd_handle_t cmd, int timeout_ms);
void i2c_cmd_link_delete(i2c_cmd_handle_t cmd);

#ifdef __cplusplus
}
#endif
