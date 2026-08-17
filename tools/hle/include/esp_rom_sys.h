#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void esp_rom_delay_us(uint32_t us);
void esp_rom_gpio_pad_select_gpio(uint32_t gpio_num);

#ifdef __cplusplus
}
#endif
