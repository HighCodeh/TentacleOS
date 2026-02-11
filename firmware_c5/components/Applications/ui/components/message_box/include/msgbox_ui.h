#ifndef MSGBOX_UI_H
#define MSGBOX_UI_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*msgbox_cb_t)(bool confirm);

void msgbox_open(const char * icon, const char * msg, const char * btn_ok, const char * btn_cancel, msgbox_cb_t cb);

void msgbox_close(void);

#ifdef __cplusplus
}
#endif

#endif