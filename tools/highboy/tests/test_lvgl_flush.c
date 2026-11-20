// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.


#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERR_NO_MEM 2
#define ESP_ERR_TIMEOUT 3
#define ESP_ERR_INVALID_STATE 4
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(ms) (ms)
#define portMAX_DELAY UINT32_MAX
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_DEFAULT 4
#define LV_DRAW_BUF_ALIGN 4
#define LCD_PANEL_W 240
#define LCD_PANEL_H 320
#define SYS_PRIO_RENDER 6
#define SYS_CORE_UI 1
#define LV_EVENT_FLUSH_START 1
#define LV_DISPLAY_ROTATION_0 0
#define LV_DISPLAY_ROTATION_270 3
typedef int esp_err_t;
typedef int BaseType_t;
typedef int lv_color_format_t;
typedef int lv_display_rotation_t;
typedef struct {int x1,y1,x2,y2;} lv_area_t;
typedef struct {uint8_t *data;struct {int stride;} header;} lv_draw_buf_t;
typedef struct {int unused;} lv_display_t;
typedef struct {void *target,*param;} lv_event_t;
typedef struct {void *(*buf_malloc_cb)(size_t,lv_color_format_t);void (*buf_free_cb)(void *);} lv_draw_buf_handlers_t;
typedef void *esp_lcd_panel_io_handle_t;
typedef void esp_lcd_panel_io_event_data_t;
typedef void (*lvgl_glue_strip_cb_t)(int32_t,int32_t,int32_t,int32_t,const uint8_t *,int32_t);
typedef struct {bool (*on_color_trans_done)(void *,void *,void *);} esp_lcd_panel_io_callbacks_t;
typedef struct {int task_priority,task_stack,task_affinity,task_max_sleep_ms,timer_period_ms;} lvgl_port_cfg_t;
typedef struct {
  void *io_handle,*panel_handle;
  int buffer_size,hres,vres;
  bool double_buffer,monochrome;
  struct {bool swap_xy,mirror_x,mirror_y;} rotation;
  struct {bool buff_dma,buff_spiram,swap_bytes;} flags;
} lvgl_port_display_cfg_t;
typedef struct {
  pthread_mutex_t mutex;
  pthread_cond_t condition;
  bool available;
  atomic_bool waiting;
} semaphore_t;
typedef semaphore_t *SemaphoreHandle_t;
static int create_calls,fail_create,live_semaphores,removed,port_starts,port_stops,lock_depth;
static bool fail_port,fail_lock,fail_display,fail_callback,bus_available=true;
static atomic_int bus_releases;
static atomic_bool wait_returned;
static void (*wait_callback)(lv_display_t *);
static void (*start_callback)(lv_event_t *);
static bool (*irq_callback)(void *,void *,void *);
static lv_display_t display;
static lv_draw_buf_handlers_t handlers,image_handlers;
static void *io_handle=&display,*panel_handle=&display;
static const char *esp_err_to_name(int error) {(void)error;return "fault";}
static void *heap_caps_malloc(size_t size,int caps) {(void)caps;return malloc(size);}
static void heap_caps_free(void *p) {free(p);}
static SemaphoreHandle_t xSemaphoreCreateBinary(void) {
  if(++create_calls==fail_create)return NULL;
  semaphore_t *s=calloc(1,sizeof(*s));assert(s);
  assert(!pthread_mutex_init(&s->mutex,NULL));assert(!pthread_cond_init(&s->condition,NULL));
  live_semaphores++;return s;
}
static void vSemaphoreDelete(semaphore_t *s) {
  assert(!atomic_load(&s->waiting));
  pthread_mutex_destroy(&s->mutex);pthread_cond_destroy(&s->condition);free(s);live_semaphores--;
}
static int xSemaphoreTake(semaphore_t *s,uint32_t ticks) {
  assert(s);pthread_mutex_lock(&s->mutex);
  if(ticks==portMAX_DELAY) {
    while(!s->available) {
      atomic_store(&s->waiting,true);
      pthread_cond_broadcast(&s->condition);
      pthread_cond_wait(&s->condition,&s->mutex);
    }
  }
  int taken=s->available;s->available=false;atomic_store(&s->waiting,false);
  pthread_mutex_unlock(&s->mutex);return taken;
}
static int xSemaphoreGiveFromISR(semaphore_t *s,int *woken) {
  assert(s);pthread_mutex_lock(&s->mutex);assert(!s->available);s->available=true;
  if(atomic_load(&s->waiting))*woken=pdTRUE;
  pthread_cond_broadcast(&s->condition);pthread_mutex_unlock(&s->mutex);return pdTRUE;
}
static bool spi_bus_lock_take(uint32_t timeout) {assert(timeout>0);return bus_available;}
static void spi_bus_lock_give_from_isr(int *hp) {(void)hp;atomic_fetch_add(&bus_releases,1);}
static int lvgl_port_init(const lvgl_port_cfg_t *cfg) {
  assert(cfg->task_affinity==SYS_CORE_UI);if(fail_port)return ESP_FAIL;port_starts++;return ESP_OK;
}
static bool lvgl_port_lock(uint32_t timeout) {(void)timeout;if(fail_lock)return false;lock_depth++;return true;}
static void lvgl_port_unlock(void) {assert(lock_depth>0);lock_depth--;}
static int lvgl_port_deinit(void) {port_stops++;return ESP_OK;}
static lv_display_t *lvgl_port_add_disp(const lvgl_port_display_cfg_t *cfg) {
  assert(lock_depth>0 && cfg->flags.buff_dma && !cfg->flags.buff_spiram);
  return fail_display?NULL:&display;
}
static int lvgl_port_remove_disp(lv_display_t *d) {assert(d==&display && lock_depth);removed++;return ESP_OK;}
static int esp_lcd_panel_io_register_event_callbacks(void *io,const esp_lcd_panel_io_callbacks_t *cb,void *ctx) {
  assert(io==io_handle && lock_depth);(void)ctx;
  if(fail_callback && cb->on_color_trans_done)return ESP_FAIL;
  irq_callback=cb->on_color_trans_done;return ESP_OK;
}
static lv_draw_buf_handlers_t *lv_draw_buf_get_handlers(void) {return &handlers;}
static lv_draw_buf_handlers_t *lv_draw_buf_get_image_handlers(void) {return &image_handlers;}
static void lv_display_add_event_cb(lv_display_t *d,void (*cb)(lv_event_t *),int event,void *ctx) {
  assert(d==&display && event==LV_EVENT_FLUSH_START && !ctx && lock_depth);start_callback=cb;
}
static void lv_display_set_flush_wait_cb(lv_display_t *d,void (*cb)(lv_display_t *)) {
  assert(d==&display && lock_depth);wait_callback=cb;
}
static void *lv_event_get_target(lv_event_t *e) {return e->target;}
static void *lv_event_get_param(lv_event_t *e) {return e->param;}
static lv_draw_buf_t *lv_display_get_buf_active(lv_display_t *d) {(void)d;return NULL;}
static void lv_display_set_rotation(lv_display_t *d,int rotation) {(void)d;(void)rotation;}
static void *lv_screen_active(void) {return &display;}
static void lv_obj_invalidate(void *o) {(void)o;}

/* DISPLAY_GLUE */

static void start_flush(void) {lv_event_t e={.target=&display};start_callback(&e);}
static void *wait_thread(void *arg) {(void)arg;wait_callback(&display);atomic_store(&wait_returned,true);return NULL;}
static void wait_until_blocked(void) {
  semaphore_t *s=s_lvgl_flush_done;
  pthread_mutex_lock(&s->mutex);
  while(!atomic_load(&s->waiting))pthread_cond_wait(&s->condition,&s->mutex);
  pthread_mutex_unlock(&s->mutex);
  assert(!atomic_load(&wait_returned));
}
static void failed_init(void) {
  assert(lvgl_glue_init()!=ESP_OK);assert(!lvgl_glue_is_ready());
  assert(!live_semaphores && !lock_depth && !s_disp && !s_trans_done && !s_lvgl_flush_done);
  assert(port_starts==port_stops);
  fail_create=0;create_calls=0;fail_port=false;fail_lock=false;fail_display=false;fail_callback=false;
}
int main(void) {
  fail_create=1;failed_init();fail_create=2;failed_init();
  fail_port=true;failed_init();fail_lock=true;failed_init();
  fail_display=true;failed_init();fail_callback=true;failed_init();assert(removed==1);
  assert(lvgl_glue_init()==ESP_OK && lvgl_glue_is_ready());
  assert(wait_callback && start_callback && irq_callback && live_semaphores==2 && !lock_depth);
  assert(lvgl_glue_init()==ESP_OK && live_semaphores==2);
  assert(!irq_callback(NULL,NULL,NULL));assert(!s_lvgl_flush_done->available);

  // DMA completed before LVGL reaches the wait: exactly one completion is consumed.
  start_flush();irq_callback(NULL,NULL,NULL);wait_callback(&display);
  assert(!s_lvgl_flush_done->available && atomic_load(&bus_releases)==1);

  // A new frame must really block; the previous completion cannot release its buffer.
  start_flush();pthread_t thread;assert(!pthread_create(&thread,NULL,wait_thread,NULL));
  wait_until_blocked();assert(irq_callback(NULL,NULL,NULL));pthread_join(thread,NULL);
  assert(atomic_load(&wait_returned) && atomic_load(&bus_releases)==2);

  // A pending LVGL strip still completes on its own semaphore during direct takeover.
  start_flush();lvgl_glue_direct_begin();irq_callback(NULL,NULL,NULL);
  assert(s_lvgl_flush_done->available && !s_trans_done->available);
  wait_callback(&display);
  irq_callback(NULL,NULL,NULL);assert(s_trans_done->available && !s_lvgl_flush_done->available);
  lvgl_glue_wait_flush(100);assert(!s_trans_done->available);lvgl_glue_direct_end();

  // Completion ownership is independent of the optional outer bus lock.
  int releases=atomic_load(&bus_releases);bus_available=false;
  start_flush();irq_callback(NULL,NULL,NULL);wait_callback(&display);
  assert(atomic_load(&bus_releases)==releases && !s_lvgl_flush_done->available);
  vSemaphoreDelete(s_trans_done);vSemaphoreDelete(s_lvgl_flush_done);
  puts("PASS: display init failure cleanup, blocking DMA wait, early/delayed completion and direct-draw routing");
}
