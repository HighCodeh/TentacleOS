#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include <stdbool.h>

typedef enum {
    SCREEN_NONE,
    SCREEN_HOME,
    SCREEN_MENU,
    SCREEN_WIFI_MENU,
    SCREEN_WIFI_SCAN,
    SCREEN_BLE_MENU,
    SCREEN_SUBGHZ_SPECTRUM,
    // others screens
} screen_id_t;

void ui_init(void);

bool ui_acquire(void);
void ui_release(void);
void ui_switch_screen(screen_id_t new_screen);

#endif


