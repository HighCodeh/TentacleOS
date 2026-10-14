// Host version of Service/lvgl/lvgl_glue.c. The firmware builds it on
// esp_lvgl_port; here the display comes from lv_port_disp_hle.cpp and a plain
// thread runs the LVGL timer loop under the same lock the UI takes.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

extern "C" {
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl_glue.h"
}

#include "hle/lv_port_hle.h"

static const char *TAG = "LVGL_GLUE";

#define LVGL_TASK_MIN_SLEEP_MS 1
#define LVGL_TASK_MAX_SLEEP_MS 500

static std::recursive_timed_mutex s_lvgl_mutex;
static std::thread s_lvgl_thread;
static std::atomic<bool> s_is_ready{false};
static std::atomic<bool> s_should_stop{false};

static void lvgl_task(void) {
    while (!s_should_stop.load(std::memory_order_acquire)) {
        uint32_t sleep_ms;
        {
            std::lock_guard<std::recursive_timed_mutex> lock(s_lvgl_mutex);
            sleep_ms = lv_timer_handler();
        }
        sleep_ms = std::clamp<uint32_t>(sleep_ms, LVGL_TASK_MIN_SLEEP_MS, LVGL_TASK_MAX_SLEEP_MS);
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
}

extern "C" {

esp_err_t lvgl_glue_init(void) {
    if (s_is_ready.load(std::memory_order_acquire)) {
        return ESP_OK;
    }
    hle_lv_port_disp_init();
    s_should_stop.store(false, std::memory_order_release);
    s_lvgl_thread = std::thread(lvgl_task);
    s_is_ready.store(true, std::memory_order_release);
    ESP_LOGI(TAG, "LVGL task started");
    return ESP_OK;
}

bool lvgl_glue_is_ready(void) { return s_is_ready.load(std::memory_order_acquire); }

// esp_lvgl_port semantics: 0 (and the -1 callers pass) block until the lock is free.
bool lvgl_glue_lock(int timeout_ms) {
    if (timeout_ms <= 0) {
        s_lvgl_mutex.lock();
        return true;
    }
    return s_lvgl_mutex.try_lock_for(std::chrono::milliseconds(timeout_ms));
}

void lvgl_glue_unlock(void) { s_lvgl_mutex.unlock(); }

// Direct drawing goes through esp_lcd_panel_draw_bitmap, which the HLE panel
// shim writes to the display synchronously, so there is no flush to wait on.
void lvgl_glue_direct_begin(void) {}
void lvgl_glue_direct_end(void) {}
void lvgl_glue_wait_flush(uint32_t timeout_ms) { (void)timeout_ms; }

bool lvgl_glue_toggle_rotation(void) { return false; }
bool lvgl_glue_is_landscape(void) { return false; }

void lvgl_glue_capture_begin(lvgl_glue_strip_cb_t cb) { (void)cb; }
void lvgl_glue_capture_end(void) {}

void hle_lvgl_glue_stop(void) {
    s_should_stop.store(true, std::memory_order_release);
    if (s_lvgl_thread.joinable()) {
        s_lvgl_thread.join();
    }
    s_is_ready.store(false, std::memory_order_release);
}

} // extern "C"
