#ifndef TEST_UI_PLATFORM_H
#define TEST_UI_PLATFORM_H
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include "ota_service.h"
#define SYS_PRIO_SERVICE_LO 4
#define SYS_CORE_RADIO 0
#define LV_OBJ_FLAG_SCROLLABLE 1
#define LV_OPA_COVER 255
#define LV_EVENT_DELETE 1
#define LV_ALIGN_TOP_MID 1
#define LV_ALIGN_BOTTOM_MID 2
#define LV_ANIM_OFF 0
#define UI_CHROME_HEADER_H 42
#define UI_CHROME_FOOTER_H 22
#define INPUT_ACTION_PRESS 1
#define INPUT_BTN_BACK 2
#define INPUT_BTN_OK 3
#define SCREEN_DEV_MENU 1
#define pdPASS 1
#define ESP_LOGE(tag, ...) ((void)(tag))
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(lock) assert(pthread_mutex_lock(lock)==0)
#define portEXIT_CRITICAL(lock) assert(pthread_mutex_unlock(lock)==0)
typedef struct {int action,button;} input_event_t;
typedef struct {char text[128];int value;} lv_obj_t;
typedef struct {int unused;} lv_timer_t;
typedef struct {lv_obj_t *target;} lv_event_t;
typedef struct {int screen_base;} theme_t;
extern theme_t current_theme;
lv_obj_t *lv_obj_create(lv_obj_t *);
lv_obj_t *lv_label_create(lv_obj_t *);
lv_obj_t *lv_bar_create(lv_obj_t *);
void lv_obj_remove_flag(lv_obj_t *,int);
void lv_obj_set_style_bg_color(lv_obj_t *,int,int);
void lv_obj_set_style_bg_opa(lv_obj_t *,int,int);
void lv_obj_set_width(lv_obj_t *,int);
void lv_obj_set_size(lv_obj_t *,int,int);
void lv_obj_align(lv_obj_t *,int,int,int);
void lv_label_set_text(lv_obj_t *,const char *);
void lv_label_set_text_fmt(lv_obj_t *,const char *,...);
void lv_bar_set_value(lv_obj_t *,int,int);
void lv_bar_set_range(lv_obj_t *,int,int);
void lv_obj_add_event_cb(lv_obj_t *,void (*)(lv_event_t *),int,void *);
lv_obj_t *lv_event_get_target(lv_event_t *);
lv_obj_t *lv_screen_active(void);
lv_timer_t *lv_timer_create(void (*)(lv_timer_t *),int,void *);
void lv_timer_delete(lv_timer_t *);
void ui_chrome_header(lv_obj_t *,const char *,const char *);
void ui_chrome_footer(lv_obj_t *,const char *);
void ui_screen_load(lv_obj_t *);
void ui_input_set_screen_handler(void (*)(const input_event_t *,void *),void *);
void ui_switch_screen(int);
int xTaskCreatePinnedToCore(void (*)(void *),const char *,uint32_t,void *,int,void *,int);
void vTaskDelete(void *);
#endif
