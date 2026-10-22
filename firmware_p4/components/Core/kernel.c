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

#include <stdlib.h>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
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
#include "battery_service.h"
#include "led_control.h"
#include "buttons_gpio.h"
#include "lf_rfid.h"
#include "audio_i2s.h"
#include "boot_sound.h"
#include "input_manager.h"
#include "bridge_manager.h"
#include "config_sync.h"
#include "spi_bridge.h"
#include "storage_init.h"
#include "storage_assets.h"
#include "boot_report.h"
#include "sys_time.h"
#include "power_manager.h"
#include "tos_first_boot.h"
#include "tos_config.h"
#include "tos_theme.h"
#include "wifi_service.h"
#include "bluetooth_service.h"
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
#include "sys_shutdown.h"

static const char *TAG = "KERNEL";

#define CONSOLE_TASK_STACK     8192
#define BOOT_SOUND_PATH        "/assets/storage/ghost_cyberpunk_boot.mp3"
#define BOOT_SETTLE_MS         1500
#define DISPLAY_RETRY_DELAY_MS 100

// Safe-mode entry: OK + BACK must be held together at power-on. Wait for the
// input sampler to debounce, then require the combo to stay held across the
// whole confirm window so a stray press never triggers it.
#define SAFE_MODE_SETTLE_MS  250
#define SAFE_MODE_CONFIRM_MS 500
#define SAFE_MODE_POLL_MS    50

static void console_task(void *pvParameters) {
  (void)pvParameters;
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

static bool init_display(void) {
  esp_err_t result = st7789_init();
  if (result != ESP_OK) {
    vTaskDelay(pdMS_TO_TICKS(DISPLAY_RETRY_DELAY_MS));
    result = st7789_init();
  }
  boot_report_record("display", true, result);
  if (result != ESP_OK) {
    sys_monitor_expect_ui(false);
    return false;
  }
  result = lvgl_glue_init();
  boot_report_record("lvgl", true, result);
  if (result != ESP_OK) {
    sys_monitor_expect_ui(false);
    return false;
  }
  lv_port_indev_init();
  sys_monitor_expect_ui(true);
  return true;
}

static void init_safe_mode(void) {
  led_rgb_init();
  led_signal_error();
  boot_report_record("battery", false, bq25896_init());
  if (init_display()) {
    ui_init_safe_mode();
  } else {
    ESP_LOGE(TAG, "Recovery display unavailable; preserving diagnostics without starting services");
  }
  ESP_ERROR_CHECK(sys_monitor_start(false));
  vTaskDelay(pdMS_TO_TICKS(BOOT_SETTLE_MS));
  boot_report_complete();
}

// Fires on any heap_caps allocation failure with the size, caps and caller -
// context the generic vApplicationMallocFailedHook lacks.
static void heap_alloc_failed_cb(size_t size, uint32_t caps, const char *function_name) {
  ESP_LOGE(TAG,
           "alloc failed: %u B, caps 0x%lx, in %s",
           (unsigned)size,
           (unsigned long)caps,
           function_name ? function_name : "?");
}

esp_err_t kernel_init(void) {
  boot_report_reset();
  heap_caps_register_failed_alloc_callback(heap_alloc_failed_cb);

  // 1. NVS
  esp_err_t ret = nvs_flash_init();
  boot_report_record("nvs", true, ret);

  sys_time_init();

  // Capture last-run crash forensics before anything can overwrite the reason.
  boot_report_capture_crash();

  sys_shutdown_init();

  // Power management: light sleep, no DFS (frequency pinned so the console UART
  // baud survives). It is gated by the NO_LIGHT_SLEEP lock, held while on external
  // power / native USB (item 41). The screen does NOT hold the lock: the backlight
  // is always on and cannot be controlled yet, so on battery the CPU may light
  // sleep with the panel lit. Set up before tasks come up.
  power_manager_init();

  // Install the GPIO ISR service once here so the install order is deterministic
  // instead of decided by whichever driver ran first. Drivers only add handlers.
  esp_err_t isr_ret = gpio_install_isr_service(0);
  if (isr_ret != ESP_OK && isr_ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(isr_ret));
  }

  // Revised High Boy V2 native 125 kHz stage: initialize its outputs low so
  // the antenna is never energized during boot.
  boot_report_record("rfid-lf", false, lf_rfid_init());

  // 2. Buses. I2C failure degrades the charger/haptic/LED but not the whole boot,
  // so it is reported (item 8) without forcing safe mode.
  spi_init();
  boot_report_record("i2c", false, init_i2c());

  input_manager_init();
  bool is_safe_mode =
      boot_report_in_bootloop() || !boot_report_all_required_ok() || detect_safe_mode_combo();
  if (!is_safe_mode) {
    boot_report_record("sd-storage", false, storage_init());
    if (storage_is_mounted()) {
      esp_err_t sd_health = storage_check_health();
      if (sd_health != ESP_OK) {
        ESP_LOGW(TAG, "SD health check failed; remounting");
        sd_health = storage_remount();
        if (sd_health == ESP_OK) {
          sd_health = storage_check_health();
        }
      }
      boot_report_record("sd-health", false, sd_health);
    }
  }
  boot_report_record("assets", true, storage_assets_init());
  storage_assets_print_info();
  if (is_safe_mode || !boot_report_all_required_ok()) {
    init_safe_mode();
    return ESP_FAIL;
  }

  // 4. Configuration, theme
  tos_first_boot_setup();
  tos_config_load_all();
  // Push the LED signal colors + brightness into the driver. Service owns the
  // config; the driver just stores the values, so it never depends on the config
  // module (keeps the Drivers <- Service dependency direction).
  led_set_signal_config(g_config_led.info_color,
                        g_config_led.warning_color,
                        g_config_led.error_color,
                        g_config_led.brightness);
  // Custom SD log tee (tos_log) removed: its esp_log_set_vprintf hook put a
  // newlib vsnprintf (~1.5 KB) on every logging task's stack and overflowed the
  // small (4 KB) driver tasks. Crash forensics now come from the coredump
  // partition (see Service/boot_report) only.

  tos_theme_load_from_sd();
  ESP_LOGI(TAG, "TentacleOS booted successfully");

  if (!init_display()) {
    led_rgb_init();
    led_signal_error();
    ESP_ERROR_CHECK(sys_monitor_start(false));
    boot_report_complete();
    return ESP_FAIL;
  }

  // 5. Peripherals
  led_rgb_init();
  boot_report_record("battery", false, bq25896_init());
  battery_service_init();
  // Haptic not initialized: the board's chip is an AW8623 (schematic sheet 6 +
  // pin_def), not the DRV2605L this driver targets, so its init NACKs. Left off
  // until the AW8623 driver lands; play_effect() no-ops while uninitialized.
  cc1101_init();
  // C5 radio coprocessor bridge (SPI2, POLL mode to match the C5 slave). Inits
  // the bridge bus, starts the link monitor, and probes the C5 version. If the
  // C5 is absent this marks the bridge down and the monitor re-probes until it
  // appears, so a late-booting C5 is still picked up. Never blocks the boot.
  bridge_manager_init();
  config_sync_start(); // push changed config files to the C5 (hash-gated, background)
  buttons_init();
  // Creates the TX mutex that serializes access to the single I2S_NUM_0 TX
  // channel, plus the UI-sound FX task. Playback still works without this (the
  // amp enable and the channel open are both lazy), but with no serialization:
  // concurrent play_* calls then fight over the channel and the loser fails.
  boot_report_record("audio", false, audio_i2s_init());
  audio_i2s_set_volume((uint8_t)g_config_system.volume);

  ui_init();
  if (g_config_screen.rotation == 2 && !lvgl_glue_is_landscape()) {
    lvgl_glue_toggle_rotation();
  }

  // 7. Services
  ESP_ERROR_CHECK(sys_monitor_start(false));
  wifi_service_init();
  if (g_config_wifi.enabled)
    wifi_service_start();
  else
    wifi_service_stop();
  if (g_config_ble.enabled) {
    bluetooth_service_init();
    bluetooth_service_start();
  } else {
    bluetooth_service_stop();
  }

  // USB-C data mux defaults to the CP2105 UART bridge (serial console / flash).
  // The user switches to native P4 USB at runtime from Connection settings.
  usb_mux_init();

  // Companion host link (USB CDC). Bridge must be up first (commands relay to C5).
  host_link_state_init();  // load toggle settings before the link comes up
  host_link_stream_init(); // streaming + heartbeat proxy state
  host_link_init();
  host_link_cdc_init();
  host_link_log_init();

  // Console last: esp_console_new_repl_uart takes over UART0 (installs the driver
  // + redirects stdout). Do it after all the boot logging above so the takeover
  // does not race concurrent writes on UART0 (that race left UART0 dead: output
  // died right at host_link_log_init and the REPL prompt never appeared).
  BaseType_t console_result = xTaskCreatePinnedToCore(console_task,
                                                      "console_task",
                                                      CONSOLE_TASK_STACK,
                                                      NULL,
                                                      SYS_PRIO_SERVICE_HI,
                                                      NULL,
                                                      SYS_CORE_RADIO);
  boot_report_record("console", false, console_result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
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
  if (required_ok && g_config_system.ui_sound_fx) {
    boot_report_record("boot-sound", false, boot_sound_play(BOOT_SOUND_PATH));
  }
  boot_report_complete();
  return required_ok ? ESP_OK : ESP_FAIL;
}

// FreeRTOS Safeguards

void safeguard_alert(const char *title, const char *message) {
  ESP_LOGE(TAG, "ALERT: %s - %s", title, message);

  if (lvgl_glue_is_ready() && ui_acquire()) {
    msgbox_open(LV_SYMBOL_WARNING, message, "OK", NULL, NULL);
    ui_release();
  }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
  (void)xTask;
  ESP_LOGE(TAG, "STACK OVERFLOW in task [%s]", pcTaskName);
  abort();
}

void vApplicationMallocFailedHook(void) {
  ESP_LOGE(TAG, "MALLOC FAILED — out of memory");
}
