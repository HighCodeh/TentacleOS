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
    SCREEN_BLE_SPAM_SELECT,
    SCREEN_BLE_SPAM,
    SCREEN_BADUSB_MENU,
    SCREEN_BADUSB_BROWSER,
    SCREEN_BADUSB_LAYOUT,
    SCREEN_BADUSB_CONNECT,
    SCREEN_BADUSB_RUNNING,
    SCREEN_SUBGHZ_SPECTRUM,
    SCREEN_SETTINGS,
    SCREEN_DISPLAY_SETTINGS,
    SCREEN_INTERFACE_SETTINGS,
    SCREEN_SOUND_SETTINGS,
    SCREEN_BATTERY_SETTINGS,
    SCREEN_CONNECTION_SETTINGS,
    SCREEN_ABOUT_SETTINGS
    // others screens
} screen_id_t;

void ui_init(void);

bool ui_acquire(void);
void ui_release(void);
void ui_switch_screen(screen_id_t new_screen);

#endif


