#ifndef TEST_SUBGHZ_CC1101_H
#define TEST_SUBGHZ_CC1101_H
#include <stdint.h>
#include "esp_err.h"
typedef int cc1101_preset_t;
uint8_t cc1101_get_active_preset_id(void);
esp_err_t cc1101_set_frequency(uint32_t frequency);
#endif
