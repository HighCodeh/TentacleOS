#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t gpio_num_t;

#define GPIO_NUM_NC (-1)
#define GPIO_NUM_0  0
#define GPIO_NUM_1  1
#define GPIO_NUM_2  2
#define GPIO_NUM_3  3
#define GPIO_NUM_4  4
#define GPIO_NUM_5  5
#define GPIO_NUM_6  6
#define GPIO_NUM_7  7
#define GPIO_NUM_8  8
#define GPIO_NUM_9  9
#define GPIO_NUM_10 10
#define GPIO_NUM_11 11
#define GPIO_NUM_12 12
#define GPIO_NUM_13 13
#define GPIO_NUM_14 14
#define GPIO_NUM_15 15
#define GPIO_NUM_16 16
#define GPIO_NUM_17 17
#define GPIO_NUM_18 18
#define GPIO_NUM_19 19
#define GPIO_NUM_20 20
#define GPIO_NUM_21 21
#define GPIO_NUM_22 22
#define GPIO_NUM_23 23
#define GPIO_NUM_24 24
#define GPIO_NUM_25 25
#define GPIO_NUM_26 26
#define GPIO_NUM_27 27
#define GPIO_NUM_32 32
#define GPIO_NUM_33 33
#define GPIO_NUM_38 38
#define GPIO_NUM_39 39
#define GPIO_NUM_40 40
#define GPIO_NUM_41 41
#define GPIO_NUM_42 42
#define GPIO_NUM_43 43
#define GPIO_NUM_44 44
#define GPIO_NUM_45 45
#define GPIO_NUM_46 46
#define GPIO_NUM_47 47
#define GPIO_NUM_48 48
#define GPIO_NUM_54 54

typedef struct {
    uint64_t pin_bit_mask;
    int mode;
    int pull_up_en;
    int pull_down_en;
    int intr_type;
} gpio_config_t;

#define GPIO_INTR_DISABLE    0
#define GPIO_INTR_POSEDGE    1
#define GPIO_INTR_NEGEDGE    2
#define GPIO_INTR_ANYEDGE    3
#define GPIO_MODE_INPUT      1
#define GPIO_MODE_OUTPUT     2
#define GPIO_MODE_INPUT_OUTPUT 3
#define GPIO_MODE_INPUT_OUTPUT_OD 4
#define GPIO_PULLUP_ENABLE    1
#define GPIO_PULLUP_DISABLE   0
#define GPIO_PULLDOWN_ENABLE  1
#define GPIO_PULLDOWN_DISABLE 0

esp_err_t gpio_config(const gpio_config_t *cfg);
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level);
int       gpio_get_level(gpio_num_t pin);
esp_err_t gpio_install_isr_service(int flags);
esp_err_t gpio_isr_handler_add(gpio_num_t pin, void (*handler)(void *), void *arg);
void      gpio_reset_pin(gpio_num_t pin);

#ifdef __cplusplus
}
#endif
