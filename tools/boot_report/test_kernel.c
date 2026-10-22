#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int esp_err_t;
typedef int BaseType_t;
typedef void *TaskHandle_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_NO_MEM 2
#define pdPASS 1
#define SYS_PRIO_SERVICE_HI 5
#define SYS_CORE_RADIO 0
#define INPUT_BTN_OK 1
#define INPUT_BTN_BACK 2
#define LV_SYMBOL_WARNING "warning"
#define pdMS_TO_TICKS(value) (value)
#define ESP_LOGE(...) fixture_log(__VA_ARGS__)
#define ESP_LOGW(...) fixture_log(__VA_ARGS__)
#define ESP_LOGI(...) fixture_log(__VA_ARGS__)
#define ESP_ERROR_CHECK(expr) assert((expr)==ESP_OK)
static void fixture_log(const char *tag,const char *format,...) {(void)tag;(void)format;}
static const char *esp_err_to_name(esp_err_t result) {(void)result;return "fixture";}
static unsigned s_sd,s_config,s_theme,s_radios,s_normal_ui,s_safe_ui,s_indev,s_lvgl,s_panel,s_monitor,s_complete,s_sound;
static int s_nvs_result,s_assets_result,s_panel_result,s_lvgl_result;
static bool s_required_ok=true,s_loop,s_combo,s_expect_ui=true;
static struct {unsigned info_color,warning_color,error_color,brightness;} g_config_led;
static struct {unsigned rotation;} g_config_screen;
static struct {unsigned volume;bool ui_sound_fx;} g_config_system={50,true};
static struct {bool enabled;} g_config_wifi,g_config_ble;
static int nvs_flash_init(void) {return s_nvs_result;}
static void boot_report_reset(void) {s_required_ok=true;}
static void boot_report_record(const char *name,bool required,int result) {(void)name;if(required && result!=0)s_required_ok=false;}
static bool boot_report_all_required_ok(void) {return s_required_ok;}
static bool boot_report_in_bootloop(void) {return s_loop;}
static void boot_report_complete(void) {++s_complete;}
static int storage_init(void) {++s_sd;return ESP_OK;}
static int storage_assets_init(void) {return s_assets_result;}
static bool storage_is_mounted(void) {return false;}
static int storage_check_health(void) {return ESP_OK;}
static int storage_remount(void) {return ESP_OK;}
static bool input_is_down(int button) {(void)button;return s_combo;}
static int st7789_init(void) {++s_panel;return s_panel_result;}
static int lvgl_glue_init(void) {assert(s_panel_result==ESP_OK);++s_lvgl;return s_lvgl_result;}
static void lv_port_indev_init(void) {assert(s_lvgl_result==ESP_OK && s_panel_result==ESP_OK);++s_indev;}
static void ui_init(void) {assert(s_indev);++s_normal_ui;}
static void ui_init_safe_mode(void) {assert(s_indev);++s_safe_ui;}
static void sys_monitor_expect_ui(bool expected) {s_expect_ui=expected;}
static int sys_monitor_start(bool verbose) {(void)verbose;++s_monitor;return ESP_OK;}
static void tos_first_boot_setup(void) {++s_config;}
static void tos_config_load_all(void) {++s_config;}
static void tos_theme_load_from_sd(void) {++s_theme;}
static void cc1101_init(void) {++s_radios;}
static void bridge_manager_init(void) {++s_radios;}
static int boot_sound_play(const char *path) {assert(path[0]=='/');++s_sound;return ESP_OK;}
static void vTaskDelay(unsigned delay) {(void)delay;}
static void vTaskDelete(void *task) {(void)task;}
static int xTaskCreatePinnedToCore(void (*task)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle,unsigned core) {
  (void)task;(void)name;(void)stack;(void)arg;(void)priority;(void)handle;(void)core;return pdPASS;
}
static void heap_caps_register_failed_alloc_callback(void (*callback)(size_t,uint32_t,const char *)) {(void)callback;}
static void led_set_signal_config(unsigned a,unsigned b,unsigned c,unsigned d) {(void)a;(void)b;(void)c;(void)d;}
static int gpio_install_isr_service(unsigned flags) {(void)flags;return ESP_OK;}
static void audio_i2s_set_volume(uint8_t volume) {(void)volume;}
static bool lvgl_glue_is_ready(void) {return s_panel_result==ESP_OK && s_lvgl_result==ESP_OK;}
static bool lvgl_glue_is_landscape(void) {return false;}
static bool lvgl_glue_toggle_rotation(void) {return true;}
static bool spi_bridge_is_alive(void) {return true;}
static bool ui_acquire(void) {return false;}
static void ui_release(void) {}
static void msgbox_open(const char *icon,const char *message,const char *button,void *cb,void *ctx) {(void)icon;(void)message;(void)button;(void)cb;(void)ctx;}
#define FAKE(name) static int name(void) {return ESP_OK;}
FAKE(led_rgb_init) FAKE(led_signal_error) FAKE(bq25896_init) FAKE(console_service_init)
FAKE(sys_time_init) FAKE(boot_report_capture_crash) FAKE(sys_shutdown_init) FAKE(power_manager_init)
FAKE(lf_rfid_init) FAKE(spi_init) FAKE(init_i2c) FAKE(input_manager_init) FAKE(storage_assets_print_info)
FAKE(battery_service_init) FAKE(config_sync_start) FAKE(buttons_init) FAKE(audio_i2s_init)
FAKE(wifi_service_init) FAKE(wifi_service_start) FAKE(wifi_service_stop)
FAKE(bluetooth_service_init) FAKE(bluetooth_service_start) FAKE(bluetooth_service_stop)
FAKE(usb_mux_init) FAKE(host_link_state_init) FAKE(host_link_stream_init) FAKE(host_link_init)
FAKE(host_link_cdc_init) FAKE(host_link_log_init) FAKE(led_signal_warning) FAKE(led_signal_info)
/* MODULE */
static void reset(void) {
  s_sd=s_config=s_theme=s_radios=s_normal_ui=s_safe_ui=s_indev=s_lvgl=s_panel=s_monitor=s_complete=s_sound=0;
  s_nvs_result=s_assets_result=s_panel_result=s_lvgl_result=ESP_OK;s_loop=s_combo=false;s_expect_ui=true;
}
static void minimal(void) {assert(s_config==0 && s_theme==0 && s_radios==0 && s_normal_ui==0 && s_sound==0 && s_monitor==1 && s_complete==1);}
int main(void) {
  reset();s_nvs_result=ESP_FAIL;assert(kernel_init()!=ESP_OK);minimal();assert(s_sd==0 && s_safe_ui==1);
  reset();s_loop=true;assert(kernel_init()!=ESP_OK);minimal();assert(s_sd==0 && s_safe_ui==1);
  reset();s_combo=true;assert(kernel_init()!=ESP_OK);minimal();assert(s_sd==0 && s_safe_ui==1);
  reset();s_assets_result=ESP_FAIL;assert(kernel_init()!=ESP_OK);minimal();assert(s_safe_ui==1);
  reset();s_loop=true;s_panel_result=ESP_FAIL;assert(kernel_init()!=ESP_OK);minimal();assert(!s_expect_ui && s_lvgl==0 && s_safe_ui==0 && s_panel==2);
  reset();s_loop=true;s_lvgl_result=ESP_FAIL;assert(kernel_init()!=ESP_OK);minimal();assert(!s_expect_ui && s_indev==0 && s_safe_ui==0);
  reset();s_lvgl_result=ESP_FAIL;assert(kernel_init()!=ESP_OK);assert(s_radios==0 && s_normal_ui==0 && s_monitor==1 && !s_expect_ui);
  reset();assert(kernel_init()==ESP_OK);assert(s_config==2 && s_theme==1 && s_radios==2 && s_normal_ui==1 && s_sound==1 && s_monitor==1 && s_complete==1);
  puts("PASS: real kernel recovery preserves NVS, skips config/SD/radios, gates UI on display/LVGL and completes healthy boot");
}
