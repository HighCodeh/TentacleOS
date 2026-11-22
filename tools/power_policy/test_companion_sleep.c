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
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "spi_protocol.h"
#define CONFIG_PM_ENABLE 1
#define CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ 240
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE -2
#define ESP_ERR_INVALID_ARG -3
#define ESP_PM_NO_LIGHT_SLEEP 2
#define SPI2_HOST 2
#define GPIO_BRIDGE_CS_PIN 23
#define GPIO_INTR_LOW_LEVEL 4
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(ms) (ms)
#define portYIELD_FROM_ISR() ((void)0)
#define ESP_LOGI(tag, ...) ((void)(tag))
typedef int esp_err_t;
typedef int BaseType_t;
typedef void *TaskHandle_t;
typedef void *esp_pm_lock_handle_t;
typedef struct { int unused; } spi_slave_transaction_t;
typedef struct {int min_freq_mhz,max_freq_mhz;bool light_sleep_enable;} esp_pm_config_t;
static int holds, cs=1, notices;
static bool spi_enabled=true, interrupt_enabled, acquire_fail, release_fail, configure_fail;
static bool disable_fail, enable_fail;
static unsigned waits, notification_time;
static void (*gpio_handler)(void *);
static int esp_pm_lock_create(int type,int arg,const char *name,void **out) {
  assert(type==ESP_PM_NO_LIGHT_SLEEP && !arg && name);*out=(void *)1;return ESP_OK;
}
static int esp_pm_lock_acquire(void *lock) {assert(lock);if(acquire_fail)return ESP_FAIL;holds++;return ESP_OK;}
static int esp_pm_lock_release(void *lock) {assert(lock && holds==1);if(release_fail)return ESP_FAIL;holds--;return ESP_OK;}
static int esp_pm_configure(const esp_pm_config_t *config) {
  assert(holds==1 && config->light_sleep_enable && config->max_freq_mhz==240 && config->min_freq_mhz==80);
  return configure_fail?ESP_FAIL:ESP_OK;
}
static int gpio_intr_disable(int pin){assert(pin==GPIO_BRIDGE_CS_PIN);interrupt_enabled=false;return ESP_OK;}
static int gpio_intr_enable(int pin){assert(pin==GPIO_BRIDGE_CS_PIN);interrupt_enabled=true;return ESP_OK;}
static int gpio_isr_handler_add(int pin,void (*handler)(void *),void *arg){assert(pin==23 && !arg);gpio_handler=handler;return ESP_OK;}
static int gpio_wakeup_enable(int pin,int type){assert(pin==23 && type==GPIO_INTR_LOW_LEVEL);return ESP_OK;}
static int esp_sleep_enable_gpio_wakeup(void){return ESP_OK;}
static int gpio_get_level(int pin){assert(pin==23);return cs;}
static void vTaskNotifyGiveFromISR(void *task,int *awakened){assert(task==(void *)2);notices++;*awakened=1;}
static void *xTaskGetCurrentTaskHandle(void){return (void *)2;}
static uint32_t ulTaskNotifyTake(int clear,unsigned timeout) {
  assert(clear);if(timeout){assert(!holds && !spi_enabled && interrupt_enabled);waits++;notification_time=timeout;gpio_handler(NULL);assert(!interrupt_enabled);}
  unsigned value=notices;notices=0;return value;
}
static int spi_slave_disable(int host){assert(host==2 && spi_enabled && holds==1);if(disable_fail)return ESP_FAIL;spi_enabled=false;return ESP_OK;}
static int spi_slave_enable(int host){assert(host==2 && !spi_enabled);if(enable_fail)return ESP_FAIL;spi_enabled=true;return ESP_OK;}
/* DRIVER */
static bool active[9];
static uint8_t ota_state;
static bool wifi_service_is_active(void){return active[0];}
static bool wifi_service_is_busy(void){return active[1];}
static bool bluetooth_service_is_initialized(void){return active[2];}
static bool session_manager_is_active(void){return active[3];}
static bool grok_wifi_is_active(void){return active[4];}
static bool service_esp_now_is_online(void){return active[5];}
static bool ir_remote_can_sleep(void){return !active[6];}
static bool board_service_can_sleep(void){return !active[7];}
static bool spi_bridge_async_scan_busy(void){return active[8];}
static void ota_service_get_status(spi_ota_status_t *status){status->state=ota_state;}
/* POLICY */
int main(void) {
  assert(!spi_slave_driver_sleep_ready());configure_fail=true;
  assert(init_sleep()==ESP_FAIL && holds==1 && !spi_slave_driver_sleep_ready());
  // A fresh simulated boot, not an unsupported second driver initialization.
  configure_fail=false;holds=0;s_wake_held=false;
  assert(init_sleep()==ESP_OK && holds==1 && spi_slave_driver_sleep_ready());
  assert(!power_manager_can_sleep());power_manager_set_ready();assert(power_manager_can_sleep());
  for(unsigned i=0;i<9;i++){active[i]=true;assert(!power_manager_can_sleep());active[i]=false;}
  for(ota_state=SPI_OTA_STATE_ERASING;ota_state<=SPI_OTA_STATE_ERROR;ota_state++)assert(!power_manager_can_sleep());
  ota_state=SPI_OTA_STATE_IDLE;
  spi_slave_transaction_t transaction;
  s_pending[0]=&transaction;
  assert(spi_slave_driver_sleep(100)==ESP_ERR_INVALID_STATE && waits==0 && spi_enabled);
  s_pending[0]=NULL;s_has_unknown_owner=true;
  assert(spi_slave_driver_sleep(100)==ESP_ERR_INVALID_STATE);s_has_unknown_owner=false;
  assert(spi_slave_driver_sleep(0)==ESP_ERR_INVALID_ARG);
  assert(spi_slave_driver_sleep(1001)==ESP_ERR_INVALID_ARG);
  assert(spi_slave_driver_sleep(100)==ESP_OK && waits==1 && notification_time==100);
  assert(holds==1 && spi_enabled && !interrupt_enabled && atomic_load(&s_sleep_task)==NULL);
  cs=0;assert(spi_slave_driver_sleep(1000)==ESP_OK && waits==1 && holds==1);cs=1;
  assert(spi_slave_driver_sleep(1000)==ESP_OK && waits==2 && notification_time==1000);
  disable_fail=true;assert(spi_slave_driver_sleep(100)==ESP_FAIL && holds==1 && spi_enabled);disable_fail=false;
  release_fail=true;assert(spi_slave_driver_sleep(100)==ESP_FAIL && holds==1 && spi_enabled && waits==2);release_fail=false;
  acquire_fail=true;assert(spi_slave_driver_sleep(100)==ESP_FAIL && spi_enabled && holds==0);
  assert(spi_slave_driver_sleep(100)==ESP_ERR_INVALID_STATE);
  acquire_fail=false;holds=1;s_wake_held=true;enable_fail=true;
  assert(spi_slave_driver_sleep(100)==ESP_FAIL && !spi_enabled && holds==1);
  puts("PASS: production C5 sleep guards, DMA ownership, GPIO wake, pending CS, deadlines and PM failures");
  return 0;
}
