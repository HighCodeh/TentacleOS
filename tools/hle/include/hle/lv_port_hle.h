#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void hle_lv_port_disp_init(void);
void hle_lv_port_indev_init(void);

extern void *indev_keypad;
extern void *main_group;

#ifdef __cplusplus
}
#endif
