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

#include "kernel.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "spi.h"
#include "i2c_init.h"
#include "st7789.h"
#include "cc1101.h"
#include "bq25896.h"
#include "led_control.h"
#include "led_signal.h"
#include "buttons_gpio.h"
#include "ys_rfid2.h"
#include "input_manager.h"
#include "bridge_manager.h"
#include "spi_bridge.h"
#include "storage_init.h"
#include "storage_assets.h"
#include "boot_report.h"
#include "power_manager.h"
#include "tos_first_boot.h"
#include "tos_config.h"
#include "tos_theme.h"
#include "wifi_service.h"
#include "console_service.h"
#include "host_link.h"
#include "host_link_ble.h"
#include "host_link_state.h"
#include "host_link_stream.h"
#include "tusb_desc.h"
#include "lvgl_glue.h"
#include "lv_port_indev.h"
#include "ui_manager.h"
#include "msgbox_ui.h"
#include "sys_monitor.h"

static const char *TAG = "KERNEL";

#define CONSOLE_TASK_STACK     8192
#define CONSOLE_TASK_PRIO      SYS_PRIO_SERVICE_HI
#define BOOT_SETTLE_MS         1500
#define DISPLAY_RETRY_DELAY_MS 100

// Safe-mode entry: OK + BACK must be held together at power-on. Wait for the
// input sampler to debounce, then require the combo to stay held across the
// whole confirm window so a stray press never triggers it.
#define SAFE_MODE_SETTLE_MS  250
#define SAFE_MODE_CONFIRM_MS 500
#define SAFE_MODE_POLL_MS    50

static void console_task(void *pvParameters) {
  console_service_init();
  vTaskDelete(NULL);
}

static bool detect_safe_mode_combo(void) {
  vTaskDelay(pdMS_TO_TICKS(SAFE_MODE_SETTLE_MS));
  if (!input_is_down(INPUT_BTN_OK) || !input_is_down(INPUT_BTN_BACK)) {
    return false;
  }

  for (uint32_t elapsed = 0; elapsed < SAFE_MODE_CONFIRM_MS; elapsed += SAFE_MODE_POLL_MS) {
    vTaskDelay(pdMS_TO_TICKS(SAFE_MODE_POLL_MS));
    if (!input_is_down(INPUT_BTN_OK) || !input_is_down(INPUT_BTN_BACK)) {
      return false;
    }
  }

  ESP_LOGW(TAG, "Safe-mode combo (OK + BACK) held: entering safe mode");
  return true;
}

static void kernel_init_safe_mode(void) {
  led_rgb_init();
  led_signal_error(); // safe mode: fault indicator
  bq25896_init();

  st7789_init();
  lvgl_glue_init();
  lv_port_indev_init();
  ui_init_safe_mode();

  sys_monitor_start(false);
  vTaskDelay(pdMS_TO_TICKS(BOOT_SETTLE_MS));
}

esp_err_t kernel_init(void) {
  boot_report_reset();

  // 1. NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    esp_err_t erase_ret = nvs_flash_erase();
    if (erase_ret != ESP_OK) {
      ESP_LOGE(TAG, "nvs erase failed: %s", esp_err_to_name(erase_ret));
    }
    ret = nvs_flash_init();
  }
  boot_report_record("nvs", true, ret);

  // Capture last-run crash forensics before anything can overwrite the reason.
  boot_report_capture_crash();

  // Power management: DFS + light sleep (gated by the NO_LIGHT_SLEEP lock, which
  // power_policy holds while the screen is on). Set up before tasks come up.
  power_manager_init();

  // 2. Buses
  spi_init();
  init_i2c();

  // 3. Storage. SD is optional (may be absent); the assets LittleFS is required
  // for icons/config/fonts, so its failure drops us into safe mode below.
  boot_report_record("sd-storage", false, storage_init());
  boot_report_record("assets", true, storage_assets_init());
  storage_assets_print_info();

  // Safe-mode entry: a boot loop was detected, a required subsystem already
  // failed, or the OK + BACK combo is held. Checked before radios/themes/services
  // so recovery comes up minimal. input_manager_init is idempotent; buttons_init
  // below is a no-op re-init on the normal path.
  input_manager_init();
  bool safe_mode =
      boot_report_in_bootloop() || !boot_report_all_required_ok() || detect_safe_mode_combo();

  // 4. Configuration, theme
  tos_first_boot_setup();
  tos_config_load_all();
  // Custom SD log tee (tos_log) removed: its esp_log_set_vprintf hook put a
  // newlib vsnprintf (~1.5 KB) on every logging task's stack and overflowed the
  // small (4 KB) driver tasks. Crash forensics now come from the coredump
  // partition (see Service/boot_report) only.

  if (safe_mode) {
    kernel_init_safe_mode();
    return boot_report_all_required_ok() ? ESP_OK : ESP_FAIL;
  }

  tos_theme_load_from_sd();
  ESP_LOGI(TAG, "TentacleOS booted successfully");

  // 5. Peripherals
  led_rgb_init();
  boot_report_record("battery", false, bq25896_init());
  cc1101_init();
  // C5 radio coprocessor bridge (SPI2, POLL mode to match the C5 slave). Inits
  // the bridge bus, starts the link monitor, and probes the C5 version. If the
  // C5 is absent this marks the bridge down and the monitor re-probes until it
  // appears, so a late-booting C5 is still picked up. Never blocks the boot.
  bridge_manager_init();
  buttons_init();
  ys_rfid2_init(NULL);

  // 6. Display + LVGL + UI. st7789_init sets up the panel handles; lvgl_glue_init
  // brings LVGL up over esp_lvgl_port (it calls lv_init and registers the
  // display); then the keypad indev and UI lock against the glue.
  esp_err_t panel_ret = st7789_init();
  if (panel_ret != ESP_OK) {
    ESP_LOGE(TAG, "display panel init failed (%s); retrying once", esp_err_to_name(panel_ret));
    vTaskDelay(pdMS_TO_TICKS(DISPLAY_RETRY_DELAY_MS));
    panel_ret = st7789_init();
  }
  boot_report_record("display", true, panel_ret);
  if (panel_ret == ESP_OK) {
    boot_report_record("lvgl", true, lvgl_glue_init());
    lv_port_indev_init();
    ui_init();
  } else {
    ESP_LOGE(TAG, "display unavailable after retry; booting headless for diagnostics");
  }

  // 7. Services
  sys_monitor_start(false);
  wifi_service_init();
  xTaskCreatePinnedToCore(console_task, "console_task", CONSOLE_TASK_STACK, NULL, CONSOLE_TASK_PRIO, NULL, SYS_CORE_RADIO);

  // USB-C data mux defaults to the CP2105 UART bridge (serial console / flash).
  // The user switches to native P4 USB at runtime from Connection settings.
  usb_mux_init();

  // Companion host link (USB CDC). Bridge must be up first (commands relay to C5).
  host_link_state_init();  // load toggle settings before the link comes up
  host_link_stream_init(); // streaming + heartbeat proxy state
  host_link_init();
  host_link_cdc_init();
  host_link_log_init();
  // C5-dependent host-link pieces disabled with the bridge: the C5 log relay and
  // the BLE relay status poller (SPI_ID_HOST_STATUS) both talk to the C5.
  // host_link_c5log_init(); // relay C5 logs (SPI_ID_SYSTEM_LOG) as source=C5 LOG frames
  // host_link_ble_init();   // BLE relay infra; advertising starts on demand

  vTaskDelay(pdMS_TO_TICKS(BOOT_SETTLE_MS));
  bool required_ok = boot_report_all_required_ok();
  if (!required_ok) {
    led_signal_error(); // a required subsystem failed (running degraded)
  } else if (!spi_bridge_is_alive()) {
    led_signal_warning(); // C5 radio coprocessor absent: degraded but usable
  } else {
    led_signal_info(); // booted clean: idle / OK indicator
  }
  return required_ok ? ESP_OK : ESP_FAIL;
}

// FreeRTOS Safeguards

void safeguard_alert(const char *title, const char *message) {
  ESP_LOGE(TAG, "ALERT: %s - %s", title, message);

  if (ui_acquire()) {
    msgbox_open(LV_SYMBOL_WARNING, message, "OK", NULL, NULL);
    ui_release();
  }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
  (void)xTask;
  ESP_LOGE(TAG, "STACK OVERFLOW in task [%s]", pcTaskName);
}

void vApplicationMallocFailedHook(void) {
  ESP_LOGE(TAG, "MALLOC FAILED — out of memory");
}
