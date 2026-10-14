#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <map>
#include <vector>
#include <mutex>
#include <string>
#include <queue>
#include <dirent.h>
#include <chrono>
#include <condition_variable>
#include <thread>
#include <utility>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_rom_crc.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "driver/spi_slave.h"
#include "driver/rmt.h"
#include "led_strip.h"
#include "sdmmc_cmd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "hle/spi_bridge_channel.h"
#include "hle/hle_display.h"
#include "hle/hle_kernel.h"
#include "pin_def.h"
#include "esp_lcd_panel_ops.h"
#include "esp_vfs_fat.h"
#include "esp_littlefs.h"
#include "esp_partition.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>

// ═══════════════════════════════════════════════════════════════════════════════
// ESP System
// ═══════════════════════════════════════════════════════════════════════════════

static constexpr size_t SHUTDOWN_HANDLERS_MAX = 5;
static shutdown_handler_t s_shutdown_handlers[SHUTDOWN_HANDLERS_MAX];

esp_err_t esp_register_shutdown_handler(shutdown_handler_t handle) {
    for (auto &slot : s_shutdown_handlers) {
        if (slot == handle) return ESP_ERR_INVALID_STATE;
        if (slot == nullptr) {
            slot = handle;
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

void esp_restart(void) {
    for (auto *handler : s_shutdown_handlers) {
        if (handler != nullptr) handler();
    }
    fprintf(stderr, "I [SYSTEM] esp_restart() called — exiting\n");
    exit(0);
}

uint32_t esp_get_minimum_free_heap_size(void) { return 8 * 1024 * 1024; }

esp_err_t esp_read_mac(uint8_t *mac, int type) {
    if (mac == nullptr) return ESP_ERR_INVALID_ARG;
    const uint8_t base[6] = {0x02, 0x48, 0x4C, 0x45, 0x00, 0x00};
    memcpy(mac, base, sizeof(base));
    mac[5] = static_cast<uint8_t>(type);
    return ESP_OK;
}

esp_reset_reason_t esp_reset_reason(void) { return ESP_RST_POWERON; }

static int hle_default_vlog(const char *fmt, va_list ap) { return vprintf(fmt, ap); }
static vprintf_like_t s_log_vprintf = hle_default_vlog;

vprintf_like_t esp_log_set_vprintf(vprintf_like_t func) {
    vprintf_like_t previous = s_log_vprintf;
    if (func) s_log_vprintf = func;
    return previous;
}

void esp_log_writev(esp_log_level_t level, const char *tag, const char *format, va_list args) {
    (void)level; (void)tag;
    s_log_vprintf(format, args);
}

uint64_t esp_timer_get_time(void) {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(now).count();
}

void esp_rom_delay_us(uint32_t us) {
    std::this_thread::sleep_for(std::chrono::microseconds(us));
}

// Same semantics as the ROM (and IDF's linux target): the running value is
// inverted on entry and exit, so results chain across calls.
uint32_t esp_rom_crc32_le(uint32_t crc, uint8_t const *buf, uint32_t len) {
    crc = ~crc;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }
    }
    return ~crc;
}

uint16_t esp_rom_crc16_le(uint16_t crc, uint8_t const *buf, uint32_t len) {
    crc = static_cast<uint16_t>(~crc);
    for (uint32_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 1) ? static_cast<uint16_t>((crc >> 1) ^ 0x8408) : crc >> 1;
        }
    }
    return static_cast<uint16_t>(~crc);
}

// esp_timer: one dispatcher thread runs every callback in deadline order, like
// IDF's ESP_TIMER_TASK dispatch. The state is never freed, so the thread can
// outlive static destruction at exit.
struct HleTimer {
    void (*callback)(void *arg);
    void *arg;
    uint64_t period_us;
    uint64_t next_us;
    bool is_active;
};

struct HleTimerState {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<HleTimer *> timers;
    bool is_dispatcher_running = false;
};

static HleTimerState &timer_state(void) {
    static auto *s_state = new HleTimerState;
    return *s_state;
}

static void timer_dispatch_loop(void) {
    HleTimerState &st = timer_state();
    std::unique_lock<std::mutex> lock(st.mutex);
    for (;;) {
        HleTimer *due = nullptr;
        for (auto *timer : st.timers) {
            if (timer->is_active && (due == nullptr || timer->next_us < due->next_us)) {
                due = timer;
            }
        }
        if (due == nullptr) {
            st.cv.wait(lock);
            continue;
        }
        const uint64_t now = esp_timer_get_time();
        if (due->next_us > now) {
            st.cv.wait_for(lock, std::chrono::microseconds(due->next_us - now));
            continue;
        }
        if (due->period_us > 0) {
            due->next_us += due->period_us;
        } else {
            due->is_active = false;
        }
        void (*callback)(void *) = due->callback;
        void *arg = due->arg;
        lock.unlock();
        callback(arg);
        lock.lock();
    }
}

static esp_err_t timer_start(esp_timer_handle_t handle, uint64_t delay_us, uint64_t period_us) {
    if (handle == nullptr) return ESP_ERR_INVALID_ARG;
    HleTimerState &st = timer_state();
    {
        std::lock_guard<std::mutex> lock(st.mutex);
        auto *timer = static_cast<HleTimer *>(handle);
        if (timer->is_active) return ESP_ERR_INVALID_STATE;
        timer->period_us = period_us;
        timer->next_us = esp_timer_get_time() + delay_us;
        timer->is_active = true;
        if (!st.is_dispatcher_running) {
            std::thread(timer_dispatch_loop).detach();
            st.is_dispatcher_running = true;
        }
    }
    st.cv.notify_all();
    return ESP_OK;
}

esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *handle) {
    if (args == nullptr || args->callback == nullptr || handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    auto *timer = new HleTimer{args->callback, args->arg, 0, 0, false};
    HleTimerState &st = timer_state();
    std::lock_guard<std::mutex> lock(st.mutex);
    st.timers.push_back(timer);
    *handle = timer;
    return ESP_OK;
}
esp_err_t esp_timer_start_periodic(esp_timer_handle_t handle, uint64_t period_us) {
    if (period_us == 0) return ESP_ERR_INVALID_ARG;
    return timer_start(handle, period_us, period_us);
}
esp_err_t esp_timer_start_once(esp_timer_handle_t handle, uint64_t timeout_us) {
    return timer_start(handle, timeout_us, 0);
}
esp_err_t esp_timer_stop(esp_timer_handle_t handle) {
    if (handle == nullptr) return ESP_ERR_INVALID_ARG;
    HleTimerState &st = timer_state();
    std::lock_guard<std::mutex> lock(st.mutex);
    auto *timer = static_cast<HleTimer *>(handle);
    if (!timer->is_active) return ESP_ERR_INVALID_STATE;
    timer->is_active = false;
    return ESP_OK;
}
esp_err_t esp_timer_delete(esp_timer_handle_t handle) {
    if (handle == nullptr) return ESP_ERR_INVALID_ARG;
    HleTimerState &st = timer_state();
    std::lock_guard<std::mutex> lock(st.mutex);
    auto *timer = static_cast<HleTimer *>(handle);
    if (timer->is_active) return ESP_ERR_INVALID_STATE;
    st.timers.erase(std::remove(st.timers.begin(), st.timers.end(), timer), st.timers.end());
    delete timer;
    return ESP_OK;
}

// ═══════════════════════════════════════════════════════════════════════════════
// FreeRTOS Implementation (pthread-based)
// ═══════════════════════════════════════════════════════════════════════════════

struct TaskNotify {
    std::mutex mutex;
    std::condition_variable cv;
    uint32_t count = 0;
};

struct TaskInfo {
    pthread_t thread;
    TaskFunction_t func;
    std::string name;
    volatile bool running = true;
    volatile bool suspended = false;
    void *params;
    SemaphoreHandle_t suspend_sem;
    TaskNotify *notify;
};

static thread_local TaskInfo *t_current_task = nullptr;

static std::mutex s_tasks_mutex;
static std::map<TaskHandle_t, TaskInfo> s_tasks;
static TickType_t s_tick_start_us = 0;

static TickType_t get_tick_count(void) {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    uint64_t us = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    if (s_tick_start_us == 0) s_tick_start_us = us;
    return (us - s_tick_start_us) / 1000;  // ms ticks
}

static void *task_wrapper(void *arg) {
    TaskInfo *info = static_cast<TaskInfo *>(arg);
    t_current_task = info;
    info->func(info->params);
    info->running = false;
    return nullptr;
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t func, const char *name, uint32_t stack_depth,
                                    void *params, UBaseType_t prio, TaskHandle_t *out_handle,
                                    BaseType_t core_id) {
    (void)stack_depth; (void)prio; (void)core_id;
    auto *info = new TaskInfo{};
    info->func = func;
    info->name = name ? name : "unnamed";
    info->params = params;
    info->suspend_sem = xSemaphoreCreateBinary();
    info->notify = new TaskNotify{};

    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    // firmware tasks request stack in words; on host use 4KB minimum or 2x requested
    size_t stack_bytes = stack_depth * sizeof(void *);
    if (stack_bytes < 65536) stack_bytes = 65536; // 64KB minimum for host threads
    pthread_attr_setstacksize(&attr, stack_bytes);
    pthread_create(&thread, &attr, task_wrapper, info);
    info->thread = thread;

    {
        std::lock_guard<std::mutex> lock(s_tasks_mutex);
        s_tasks[info] = *info;
    }

    if (out_handle) *out_handle = info;
    return pdPASS;
}

void vTaskDelete(TaskHandle_t handle) {
    if (!handle || handle == t_current_task) {
        pthread_exit(nullptr);
    }
    auto *info = static_cast<TaskInfo *>(handle);
    {
        std::lock_guard<std::mutex> lock(s_tasks_mutex);
        info->running = false;
        s_tasks.erase(handle);
    }
    pthread_detach(info->thread);
    delete info;
}

void vTaskDelay(TickType_t ticks) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ticks * portTICK_PERIOD_MS));
}

TickType_t xTaskGetTickCount(void) { return get_tick_count(); }
char *pcTaskGetName(TaskHandle_t handle) { (void)handle; return (char *)"shim_task"; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return t_current_task; }

// Threads the firmware did not create (main, LVGL) still get a private
// notification slot, so a wait there blocks instead of spinning.
static TaskNotify *current_task_notify(void) {
    static thread_local TaskNotify s_thread_notify;
    return t_current_task != nullptr ? t_current_task->notify : &s_thread_notify;
}

uint32_t ulTaskNotifyTake(BaseType_t clear_on_exit, TickType_t ticks_to_wait) {
    TaskNotify *notify = current_task_notify();
    std::unique_lock<std::mutex> lock(notify->mutex);
    auto has_notification = [notify] { return notify->count > 0; };
    if (ticks_to_wait == portMAX_DELAY) {
        notify->cv.wait(lock, has_notification);
    } else if (!notify->cv.wait_for(lock, std::chrono::milliseconds(ticks_to_wait * portTICK_PERIOD_MS),
                                    has_notification)) {
        return 0;
    }
    const uint32_t value = notify->count;
    notify->count = clear_on_exit ? 0 : notify->count - 1;
    return value;
}

BaseType_t xTaskNotifyGive(TaskHandle_t handle) {
    if (handle == nullptr) return pdFAIL;
    TaskNotify *notify = static_cast<TaskInfo *>(handle)->notify;
    {
        std::lock_guard<std::mutex> lock(notify->mutex);
        notify->count++;
    }
    notify->cv.notify_all();
    return pdPASS;
}

void vTaskNotifyGiveFromISR(TaskHandle_t handle, BaseType_t *higher_prio_woken) {
    xTaskNotifyGive(handle);
    if (higher_prio_woken != nullptr) *higher_prio_woken = pdFALSE;
}

void vTaskSuspend(TaskHandle_t handle) {
    if (!handle) return;
    auto *info = static_cast<TaskInfo *>(handle);
    info->suspended = true;
    xSemaphoreTake(info->suspend_sem, portMAX_DELAY);
}

void vTaskResume(TaskHandle_t handle) {
    if (!handle) return;
    auto *info = static_cast<TaskInfo *>(handle);
    info->suspended = false;
    xSemaphoreGive(info->suspend_sem);
}

UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t handle) {
    (void)handle;
    return 4096;  // enough for any shim task
}

TaskHandle_t xTaskCreateStatic(TaskFunction_t func, const char *name, uint32_t stack_depth,
                                void *params, UBaseType_t prio, StackType_t *stack_buf,
                                StaticTask_t *task_buf) {
    if (!func || !stack_buf || !task_buf || stack_depth == 0) return nullptr;
    TaskHandle_t handle = nullptr;
    xTaskCreatePinnedToCore(func, name, stack_depth, params, prio, &handle, 0);
    return handle;
}

TaskHandle_t xTaskCreateStaticPinnedToCore(TaskFunction_t func, const char *name,
                                           uint32_t stack_depth, void *params, UBaseType_t prio,
                                           StackType_t *stack_buf, StaticTask_t *task_buf,
                                           BaseType_t core_id) {
    (void)core_id;
    return xTaskCreateStatic(func, name, stack_depth, params, prio, stack_buf, task_buf);
}

// ═══════════════════════════════════════════════════════════════════════════════
// FreeRTOS Semaphores
// ═══════════════════════════════════════════════════════════════════════════════

struct SemaphoreInfo {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int count;
    int max_count;
    bool is_mutex;
};

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    auto *s = new SemaphoreInfo{};
    s->is_mutex = true;
    s->max_count = 1;
    s->count = 1;
    pthread_mutex_init(&s->mutex, nullptr);
    pthread_cond_init(&s->cond, nullptr);
    return s;
}

SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    auto *s = new SemaphoreInfo{};
    s->is_mutex = false;
    s->max_count = 1;
    s->count = 0;
    pthread_mutex_init(&s->mutex, nullptr);
    pthread_cond_init(&s->cond, nullptr);
    return s;
}

SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t max, UBaseType_t initial) {
    auto *s = new SemaphoreInfo{};
    s->is_mutex = false;
    s->max_count = max;
    s->count = initial;
    pthread_mutex_init(&s->mutex, nullptr);
    pthread_cond_init(&s->cond, nullptr);
    return s;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t timeout) {
    if (!sem) return pdFALSE;
    auto *s = static_cast<SemaphoreInfo *>(sem);
    pthread_mutex_lock(&s->mutex);
    int result = 0;
    while (s->count == 0 && result == 0) {
        if (timeout == portMAX_DELAY) {
            pthread_cond_wait(&s->cond, &s->mutex);
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += timeout / 1000;
            ts.tv_nsec += (timeout % 1000) * 1000000;
            if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
            result = pthread_cond_timedwait(&s->cond, &s->mutex, &ts);
        }
    }
    if (result == 0 && s->count > 0) {
        s->count--;
        pthread_mutex_unlock(&s->mutex);
        return pdTRUE;
    }
    pthread_mutex_unlock(&s->mutex);
    return pdFALSE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t sem) {
    if (!sem) return pdFALSE;
    auto *s = static_cast<SemaphoreInfo *>(sem);
    pthread_mutex_lock(&s->mutex);
    if (s->count < s->max_count) {
        s->count++;
    }
    pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->mutex);
    return pdTRUE;
}

BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t sem, BaseType_t *woken) {
    (void)woken;
    return xSemaphoreGive(sem);
}

void vSemaphoreDelete(SemaphoreHandle_t sem) {
    if (!sem) return;
    auto *s = static_cast<SemaphoreInfo *>(sem);
    pthread_mutex_destroy(&s->mutex);
    pthread_cond_destroy(&s->cond);
    delete s;
}

// ═══════════════════════════════════════════════════════════════════════════════
// FreeRTOS Queues
// ═══════════════════════════════════════════════════════════════════════════════

struct QueueInfo {
    std::queue<std::vector<uint8_t>> items;
    size_t item_size;
    size_t max_items;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
};

QueueHandle_t xQueueCreate(UBaseType_t queue_len, UBaseType_t item_size) {
    auto *q = new QueueInfo{};
    q->max_items = queue_len;
    q->item_size = item_size;
    pthread_mutex_init(&q->mutex, nullptr);
    pthread_cond_init(&q->cond, nullptr);
    return q;
}

QueueHandle_t xQueueCreateStatic(UBaseType_t queue_len, UBaseType_t item_size,
                                  uint8_t *storage, StaticQueue_t *queue_buf) {
    if (!queue_len || !item_size || !storage || !queue_buf) return nullptr;
    return xQueueCreate(queue_len, item_size);
}

BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t timeout) {
    if (!queue) return pdFALSE;
    auto *q = static_cast<QueueInfo *>(queue);
    pthread_mutex_lock(&q->mutex);
    int result = 0;
    while (q->items.size() >= q->max_items && result == 0) {
        if (timeout == portMAX_DELAY) {
            pthread_cond_wait(&q->cond, &q->mutex);
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += timeout / 1000;
            ts.tv_nsec += (timeout % 1000) * 1000000;
            result = pthread_cond_timedwait(&q->cond, &q->mutex, &ts);
        }
    }
    if (q->items.size() < q->max_items) {
        std::vector<uint8_t> buf((const uint8_t *)item, (const uint8_t *)item + q->item_size);
        q->items.push(std::move(buf));
        pthread_cond_signal(&q->cond);
        pthread_mutex_unlock(&q->mutex);
        return pdTRUE;
    }
    pthread_mutex_unlock(&q->mutex);
    return pdFALSE;
}

BaseType_t xQueueSendFromISR(QueueHandle_t queue, const void *item, BaseType_t *higher_prio_woken) {
    if (higher_prio_woken) *higher_prio_woken = pdFALSE;
    if (!queue || !item) return pdFALSE;
    auto *q = static_cast<QueueInfo *>(queue);
    if (pthread_mutex_trylock(&q->mutex) != 0) return pdFALSE;
    if (q->items.size() >= q->max_items) {
        pthread_mutex_unlock(&q->mutex);
        return pdFALSE;
    }
    std::vector<uint8_t> buf((const uint8_t *)item, (const uint8_t *)item + q->item_size);
    q->items.push(std::move(buf));
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mutex);
    return pdTRUE;
}

BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *item) {
    if (!queue) return pdFALSE;
    auto *q = static_cast<QueueInfo *>(queue);
    pthread_mutex_lock(&q->mutex);
    std::queue<std::vector<uint8_t>>().swap(q->items);
    q->items.emplace((const uint8_t *)item, (const uint8_t *)item + q->item_size);
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mutex);
    return pdPASS;
}

BaseType_t xQueueReset(QueueHandle_t queue) {
    if (!queue) return pdFALSE;
    auto *q = static_cast<QueueInfo *>(queue);
    pthread_mutex_lock(&q->mutex);
    std::queue<std::vector<uint8_t>>().swap(q->items);
    pthread_cond_broadcast(&q->cond);
    pthread_mutex_unlock(&q->mutex);
    return pdPASS;
}

BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t timeout) {
    if (!queue || !item) return pdFALSE;
    auto *q = static_cast<QueueInfo *>(queue);
    pthread_mutex_lock(&q->mutex);
    int result = 0;
    while (q->items.empty() && result == 0) {
        if (timeout == portMAX_DELAY) {
            pthread_cond_wait(&q->cond, &q->mutex);
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += timeout / 1000;
            ts.tv_nsec += (timeout % 1000) * 1000000;
            result = pthread_cond_timedwait(&q->cond, &q->mutex, &ts);
        }
    }
    if (!q->items.empty()) {
        memcpy(item, q->items.front().data(), q->item_size);
        q->items.pop();
        pthread_cond_signal(&q->cond);
        pthread_mutex_unlock(&q->mutex);
        return pdTRUE;
    }
    pthread_mutex_unlock(&q->mutex);
    return pdFALSE;
}

void vQueueDelete(QueueHandle_t queue) {
    if (!queue) return;
    auto *q = static_cast<QueueInfo *>(queue);
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond);
    delete q;
}

// ═══════════════════════════════════════════════════════════════════════════════
// NVS Flash Mock
// ═══════════════════════════════════════════════════════════════════════════════

enum class NVSValueType {
    I8,
    U8,
    I16,
    U16,
    I32,
    U32,
    I64,
    U64,
    String,
    Blob,
};

struct NVSValue {
    NVSValueType type;
    std::vector<uint8_t> data;
};

struct NVSNamespace {
    std::map<std::string, NVSValue> keys;
};

struct NVSHandleInfo {
    std::string namespace_name;
    nvs_open_mode_t mode;
};

static constexpr size_t NVS_NAME_MAX_LENGTH = 15;
static std::mutex s_nvs_mutex;
static std::map<std::string, NVSNamespace> s_nvs_storage;
static uint32_t s_nvs_next_handle = 1;
static std::map<uint32_t, NVSHandleInfo> s_nvs_handles;
static bool s_nvs_initialized = false;

static esp_err_t nvs_validate_namespace(const char *name) {
    if (name == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t length = strlen(name);
    if (length == 0 || length > NVS_NAME_MAX_LENGTH) {
        return ESP_ERR_NVS_INVALID_NAME;
    }
    return ESP_OK;
}

static esp_err_t nvs_validate_key(const char *key) {
    if (key == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t length = strlen(key);
    if (length == 0) {
        return ESP_ERR_NVS_INVALID_NAME;
    }
    if (length > NVS_NAME_MAX_LENGTH) {
        return ESP_ERR_NVS_KEY_TOO_LONG;
    }
    return ESP_OK;
}

static std::map<uint32_t, NVSHandleInfo>::iterator nvs_find_handle(nvs_handle_t handle) {
    return s_nvs_handles.find(handle);
}

static bool nvs_handle_is_writable(const NVSHandleInfo &handle) {
    return handle.mode == NVS_READWRITE;
}

esp_err_t nvs_flash_init(void) {
    std::lock_guard<std::mutex> lock(s_nvs_mutex);
    s_nvs_initialized = true;
    return ESP_OK;
}

esp_err_t nvs_flash_erase(void) {
    std::lock_guard<std::mutex> lock(s_nvs_mutex);
    s_nvs_storage.clear();
    s_nvs_handles.clear();
    s_nvs_next_handle = 1;
    s_nvs_initialized = false;
    return ESP_OK;
}

esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t mode,
                   nvs_handle_t *out_handle) {
    const esp_err_t name_error = nvs_validate_namespace(namespace_name);
    if (name_error != ESP_OK || out_handle == nullptr) {
        return name_error != ESP_OK ? name_error : ESP_ERR_INVALID_ARG;
    }
    if (mode != NVS_READONLY && mode != NVS_READWRITE) {
        return ESP_ERR_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(s_nvs_mutex);
    if (!s_nvs_initialized) {
        return ESP_ERR_NVS_NOT_INITIALIZED;
    }

    auto namespace_it = s_nvs_storage.find(namespace_name);
    if (namespace_it == s_nvs_storage.end()) {
        if (mode == NVS_READONLY) {
            return ESP_ERR_NVS_NOT_FOUND;
        }
        s_nvs_storage.emplace(namespace_name, NVSNamespace{});
    }

    const uint32_t handle = s_nvs_next_handle++;
    s_nvs_handles.emplace(handle, NVSHandleInfo{namespace_name, mode});
    *out_handle = handle;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle) {
    std::lock_guard<std::mutex> lock(s_nvs_mutex);
    s_nvs_handles.erase(handle);
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key) {
    const esp_err_t key_error = nvs_validate_key(key);
    if (key_error != ESP_OK) {
        return key_error;
    }

    std::lock_guard<std::mutex> lock(s_nvs_mutex);
    auto handle_it = nvs_find_handle(handle);
    if (handle_it == s_nvs_handles.end()) {
        return ESP_ERR_NVS_INVALID_HANDLE;
    }
    if (!nvs_handle_is_writable(handle_it->second)) {
        return ESP_ERR_NVS_READ_ONLY;
    }

    auto &keys = s_nvs_storage[handle_it->second.namespace_name].keys;
    auto key_it = keys.find(key);
    if (key_it == keys.end()) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    keys.erase(key_it);
    return ESP_OK;
}

esp_err_t nvs_erase_all(nvs_handle_t handle) {
    std::lock_guard<std::mutex> lock(s_nvs_mutex);
    auto handle_it = nvs_find_handle(handle);
    if (handle_it == s_nvs_handles.end()) {
        return ESP_ERR_NVS_INVALID_HANDLE;
    }
    if (!nvs_handle_is_writable(handle_it->second)) {
        return ESP_ERR_NVS_READ_ONLY;
    }
    s_nvs_storage[handle_it->second.namespace_name].keys.clear();
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle) {
    std::lock_guard<std::mutex> lock(s_nvs_mutex);
    return nvs_find_handle(handle) != s_nvs_handles.end() ? ESP_OK
                                                          : ESP_ERR_NVS_INVALID_HANDLE;
}

template<typename T>
static esp_err_t nvs_get_value(nvs_handle_t handle, const char *key, T *out_value,
                               NVSValueType expected_type) {
    const esp_err_t key_error = nvs_validate_key(key);
    if (key_error != ESP_OK || out_value == nullptr) {
        return key_error != ESP_OK ? key_error : ESP_ERR_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(s_nvs_mutex);
    auto handle_it = nvs_find_handle(handle);
    if (handle_it == s_nvs_handles.end()) {
        return ESP_ERR_NVS_INVALID_HANDLE;
    }

    auto &keys = s_nvs_storage[handle_it->second.namespace_name].keys;
    auto key_it = keys.find(key);
    if (key_it == keys.end()) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (key_it->second.type != expected_type) {
        return ESP_ERR_NVS_TYPE_MISMATCH;
    }
    memcpy(out_value, key_it->second.data.data(), sizeof(T));
    return ESP_OK;
}

template<typename T>
static esp_err_t nvs_set_value(nvs_handle_t handle, const char *key, T value,
                               NVSValueType type) {
    const esp_err_t key_error = nvs_validate_key(key);
    if (key_error != ESP_OK) {
        return key_error;
    }

    std::lock_guard<std::mutex> lock(s_nvs_mutex);
    auto handle_it = nvs_find_handle(handle);
    if (handle_it == s_nvs_handles.end()) {
        return ESP_ERR_NVS_INVALID_HANDLE;
    }
    if (!nvs_handle_is_writable(handle_it->second)) {
        return ESP_ERR_NVS_READ_ONLY;
    }

    NVSValue stored_value{type, std::vector<uint8_t>(sizeof(T))};
    memcpy(stored_value.data.data(), &value, sizeof(T));
    s_nvs_storage[handle_it->second.namespace_name].keys[key] = std::move(stored_value);
    return ESP_OK;
}

esp_err_t nvs_get_i8(nvs_handle_t handle, const char *key, int8_t *out_value) {
    return nvs_get_value(handle, key, out_value, NVSValueType::I8);
}
esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *out_value) {
    return nvs_get_value(handle, key, out_value, NVSValueType::U8);
}
esp_err_t nvs_get_i16(nvs_handle_t handle, const char *key, int16_t *out_value) {
    return nvs_get_value(handle, key, out_value, NVSValueType::I16);
}
esp_err_t nvs_get_u16(nvs_handle_t handle, const char *key, uint16_t *out_value) {
    return nvs_get_value(handle, key, out_value, NVSValueType::U16);
}
esp_err_t nvs_get_i32(nvs_handle_t handle, const char *key, int32_t *out_value) {
    return nvs_get_value(handle, key, out_value, NVSValueType::I32);
}
esp_err_t nvs_get_u32(nvs_handle_t handle, const char *key, uint32_t *out_value) {
    return nvs_get_value(handle, key, out_value, NVSValueType::U32);
}
esp_err_t nvs_get_i64(nvs_handle_t handle, const char *key, int64_t *out_value) {
    return nvs_get_value(handle, key, out_value, NVSValueType::I64);
}
esp_err_t nvs_get_u64(nvs_handle_t handle, const char *key, uint64_t *out_value) {
    return nvs_get_value(handle, key, out_value, NVSValueType::U64);
}

esp_err_t nvs_set_i8(nvs_handle_t handle, const char *key, int8_t value) {
    return nvs_set_value(handle, key, value, NVSValueType::I8);
}
esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value) {
    return nvs_set_value(handle, key, value, NVSValueType::U8);
}
esp_err_t nvs_set_i16(nvs_handle_t handle, const char *key, int16_t value) {
    return nvs_set_value(handle, key, value, NVSValueType::I16);
}
esp_err_t nvs_set_u16(nvs_handle_t handle, const char *key, uint16_t value) {
    return nvs_set_value(handle, key, value, NVSValueType::U16);
}
esp_err_t nvs_set_i32(nvs_handle_t handle, const char *key, int32_t value) {
    return nvs_set_value(handle, key, value, NVSValueType::I32);
}
esp_err_t nvs_set_u32(nvs_handle_t handle, const char *key, uint32_t value) {
    return nvs_set_value(handle, key, value, NVSValueType::U32);
}
esp_err_t nvs_set_i64(nvs_handle_t handle, const char *key, int64_t value) {
    return nvs_set_value(handle, key, value, NVSValueType::I64);
}
esp_err_t nvs_set_u64(nvs_handle_t handle, const char *key, uint64_t value) {
    return nvs_set_value(handle, key, value, NVSValueType::U64);
}

static esp_err_t nvs_get_buffer(nvs_handle_t handle, const char *key, void *out_value,
                                size_t *in_out_length, NVSValueType expected_type) {
    const esp_err_t key_error = nvs_validate_key(key);
    if (key_error != ESP_OK || in_out_length == nullptr) {
        return key_error != ESP_OK ? key_error : ESP_ERR_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(s_nvs_mutex);
    auto handle_it = nvs_find_handle(handle);
    if (handle_it == s_nvs_handles.end()) {
        return ESP_ERR_NVS_INVALID_HANDLE;
    }

    auto &keys = s_nvs_storage[handle_it->second.namespace_name].keys;
    auto key_it = keys.find(key);
    if (key_it == keys.end()) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (key_it->second.type != expected_type) {
        return ESP_ERR_NVS_TYPE_MISMATCH;
    }

    const size_t required_length = key_it->second.data.size();
    if (out_value == nullptr) {
        *in_out_length = required_length;
        return ESP_OK;
    }
    if (*in_out_length < required_length) {
        *in_out_length = required_length;
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    if (required_length > 0) {
        memcpy(out_value, key_it->second.data.data(), required_length);
    }
    *in_out_length = required_length;
    return ESP_OK;
}

static esp_err_t nvs_set_buffer(nvs_handle_t handle, const char *key, const void *value,
                                size_t length, NVSValueType type) {
    const esp_err_t key_error = nvs_validate_key(key);
    if (key_error != ESP_OK || (value == nullptr && length > 0)) {
        return key_error != ESP_OK ? key_error : ESP_ERR_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(s_nvs_mutex);
    auto handle_it = nvs_find_handle(handle);
    if (handle_it == s_nvs_handles.end()) {
        return ESP_ERR_NVS_INVALID_HANDLE;
    }
    if (!nvs_handle_is_writable(handle_it->second)) {
        return ESP_ERR_NVS_READ_ONLY;
    }

    NVSValue stored_value{type, {}};
    if (length > 0) {
        const auto *bytes = static_cast<const uint8_t *>(value);
        stored_value.data.assign(bytes, bytes + length);
    }
    s_nvs_storage[handle_it->second.namespace_name].keys[key] = std::move(stored_value);
    return ESP_OK;
}

esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out_value,
                      size_t *in_out_length) {
    return nvs_get_buffer(handle, key, out_value, in_out_length, NVSValueType::String);
}

esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value) {
    if (value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    return nvs_set_buffer(handle, key, value, strlen(value) + 1, NVSValueType::String);
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value,
                       size_t *in_out_length) {
    return nvs_get_buffer(handle, key, out_value, in_out_length, NVSValueType::Blob);
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value,
                       size_t length) {
    return nvs_set_buffer(handle, key, value, length, NVSValueType::Blob);
}

// Iterators snapshot the matching entries when created, which is enough for
// the list-then-read pattern the firmware uses.
struct nvs_opaque_iterator_t {
    std::vector<nvs_entry_info_t> entries;
    size_t index = 0;
};

static nvs_type_t nvs_entry_type(NVSValueType type) {
    switch (type) {
    case NVSValueType::I8: return NVS_TYPE_I8;
    case NVSValueType::U8: return NVS_TYPE_U8;
    case NVSValueType::I16: return NVS_TYPE_I16;
    case NVSValueType::U16: return NVS_TYPE_U16;
    case NVSValueType::I32: return NVS_TYPE_I32;
    case NVSValueType::U32: return NVS_TYPE_U32;
    case NVSValueType::I64: return NVS_TYPE_I64;
    case NVSValueType::U64: return NVS_TYPE_U64;
    case NVSValueType::String: return NVS_TYPE_STR;
    case NVSValueType::Blob: return NVS_TYPE_BLOB;
    }
    return NVS_TYPE_ANY;
}

esp_err_t nvs_entry_find(const char *part_name, const char *namespace_name, nvs_type_t type,
                         nvs_iterator_t *output_iterator) {
    (void)part_name;
    if (output_iterator == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    *output_iterator = nullptr;

    auto *iterator = new nvs_opaque_iterator_t{};
    {
        std::lock_guard<std::mutex> lock(s_nvs_mutex);
        for (const auto &[ns_name, ns] : s_nvs_storage) {
            if (namespace_name != nullptr && ns_name != namespace_name) {
                continue;
            }
            for (const auto &[key, value] : ns.keys) {
                const nvs_type_t entry_type = nvs_entry_type(value.type);
                if (type != NVS_TYPE_ANY && entry_type != type) {
                    continue;
                }
                nvs_entry_info_t info{};
                strncpy(info.namespace_name, ns_name.c_str(), sizeof(info.namespace_name) - 1);
                strncpy(info.key, key.c_str(), sizeof(info.key) - 1);
                info.type = entry_type;
                iterator->entries.push_back(info);
            }
        }
    }

    if (iterator->entries.empty()) {
        delete iterator;
        return ESP_ERR_NVS_NOT_FOUND;
    }
    *output_iterator = iterator;
    return ESP_OK;
}

esp_err_t nvs_entry_next(nvs_iterator_t *iterator) {
    if (iterator == nullptr || *iterator == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (++(*iterator)->index >= (*iterator)->entries.size()) {
        delete *iterator;
        *iterator = nullptr;
        return ESP_ERR_NVS_NOT_FOUND;
    }
    return ESP_OK;
}

esp_err_t nvs_entry_info(const nvs_iterator_t iterator, nvs_entry_info_t *out_info) {
    if (iterator == nullptr || out_info == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_info = iterator->entries[iterator->index];
    return ESP_OK;
}

void nvs_release_iterator(nvs_iterator_t iterator) { delete iterator; }

// ═══════════════════════════════════════════════════════════════════════════════
// WiFi Stubs
// ═══════════════════════════════════════════════════════════════════════════════

int esp_wifi_init(const void *cfg) { (void)cfg; return ESP_OK; }
int esp_wifi_start(void) { return ESP_OK; }
int esp_wifi_stop(void) { return ESP_OK; }
int esp_wifi_disconnect(void) { return ESP_OK; }
int esp_wifi_scan_start(const void *cfg, bool block) { (void)cfg; (void)block; return ESP_OK; }
int esp_wifi_scan_stop(void) { return ESP_OK; }
int esp_wifi_scan_get_ap_num(uint16_t *num) { if (num) *num = 0; return ESP_OK; }
int esp_wifi_scan_get_ap_records(uint16_t *num, wifi_ap_record_t *rec) { (void)num; (void)rec; return ESP_OK; }
int esp_wifi_set_promiscuous(bool enable) { (void)enable; return ESP_OK; }
int esp_wifi_set_promiscuous_filter(const wifi_promiscuous_filter_t *filter) { (void)filter; return ESP_OK; }
int esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_cb_t cb) { (void)cb; return ESP_OK; }
int esp_wifi_set_channel(uint8_t primary, int secondary) { (void)primary; (void)secondary; return ESP_OK; }
int esp_wifi_get_channel(uint8_t *primary, int *secondary) { if (primary) *primary = 6; if (secondary) *secondary = 0; return ESP_OK; }

// ═══════════════════════════════════════════════════════════════════════════════
// GPIO Stubs
// ═══════════════════════════════════════════════════════════════════════════════

static std::mutex s_gpio_mutex;
static std::map<gpio_num_t, bool> s_gpio_levels;


esp_err_t gpio_config(const gpio_config_t *cfg) {
    if (!cfg) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_gpio_mutex);
    for (gpio_num_t pin = 0; pin < 64; ++pin) {
        if ((cfg->pin_bit_mask & (1ULL << pin)) == 0) continue;
        if ((cfg->mode == GPIO_MODE_INPUT || cfg->mode == GPIO_MODE_INPUT_OUTPUT) && cfg->pull_up_en) {
            s_gpio_levels[pin] = true;
        } else if ((cfg->mode == GPIO_MODE_INPUT || cfg->mode == GPIO_MODE_INPUT_OUTPUT) && cfg->pull_down_en) {
            s_gpio_levels[pin] = false;
        }
    }
    return ESP_OK;
}
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level) {
    std::lock_guard<std::mutex> lock(s_gpio_mutex);
    s_gpio_levels[pin] = (level != 0);
    return ESP_OK;
}
int gpio_get_level(gpio_num_t pin) {
    std::lock_guard<std::mutex> lock(s_gpio_mutex);
    auto it = s_gpio_levels.find(pin);
    return (it != s_gpio_levels.end() && it->second) ? 1 : 0;
}
esp_err_t gpio_install_isr_service(int flags) { (void)flags; return ESP_OK; }
esp_err_t gpio_isr_handler_add(gpio_num_t pin, void (*handler)(void *), void *arg) {
    (void)pin; (void)handler; (void)arg; return ESP_OK;
}
void gpio_reset_pin(gpio_num_t pin) { (void)pin; }
esp_err_t gpio_set_drive_capability(gpio_num_t pin, gpio_drive_cap_t strength) {
    (void)pin; (void)strength; return ESP_OK;
}
esp_err_t gpio_set_pull_mode(gpio_num_t pin, gpio_pull_mode_t pull) {
    std::lock_guard<std::mutex> lock(s_gpio_mutex);
    if (pull == GPIO_PULLUP_ONLY || pull == GPIO_PULLUP_PULLDOWN) {
        s_gpio_levels[pin] = true;
    } else if (pull == GPIO_PULLDOWN_ONLY) {
        s_gpio_levels[pin] = false;
    }
    return ESP_OK;
}
esp_err_t gpio_set_direction(gpio_num_t pin, int mode) { (void)pin; (void)mode; return ESP_OK; }
esp_err_t gpio_wakeup_enable(gpio_num_t pin, int intr_type) {
    (void)pin; (void)intr_type; return ESP_OK;
}

extern "C" void hle_set_button_mask(unsigned char mask) {
    std::lock_guard<std::mutex> lock(s_gpio_mutex);

    s_gpio_levels[GPIO_BTN_UP_PIN] = (mask & (1u << 0)) == 0;
    s_gpio_levels[GPIO_BTN_DOWN_PIN] = (mask & (1u << 1)) == 0;
    s_gpio_levels[GPIO_BTN_LEFT_PIN] = (mask & (1u << 2)) == 0;
    s_gpio_levels[GPIO_BTN_RIGHT_PIN] = (mask & (1u << 3)) == 0;
    s_gpio_levels[GPIO_BTN_OK_PIN] = (mask & (1u << 4)) == 0;
    s_gpio_levels[GPIO_BTN_BACK_PIN] = (mask & (1u << 5)) == 0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// I2C Stubs
// ═══════════════════════════════════════════════════════════════════════════════

esp_err_t i2c_param_config(i2c_port_t port, const i2c_config_t *cfg) { (void)port; (void)cfg; return ESP_OK; }
esp_err_t i2c_driver_install(i2c_port_t port, int mode, int rx_buf, int tx_buf, int flags) {
    (void)port; (void)mode; (void)rx_buf; (void)tx_buf; (void)flags; return ESP_OK;
}
i2c_cmd_handle_t i2c_cmd_link_create(void) { return 0; }
esp_err_t i2c_master_start(i2c_cmd_handle_t cmd) { (void)cmd; return ESP_OK; }
esp_err_t i2c_master_write_byte(i2c_cmd_handle_t cmd, uint8_t data, bool ack_en) {
    (void)cmd; (void)data; (void)ack_en; return ESP_OK;
}
esp_err_t i2c_master_read_byte(i2c_cmd_handle_t cmd, uint8_t *data, int ack) {
    (void)cmd; (void)ack; if (data) *data = 0; return ESP_OK;
}
esp_err_t i2c_master_stop(i2c_cmd_handle_t cmd) { (void)cmd; return ESP_OK; }
esp_err_t i2c_master_cmd_begin(i2c_port_t port, i2c_cmd_handle_t cmd, int timeout_ms) {
    (void)port; (void)cmd; (void)timeout_ms; return ESP_OK;
}
void i2c_cmd_link_delete(i2c_cmd_handle_t cmd) { (void)cmd; }

// New master API: an empty bus. Transfers fail the way IDF 5.5 reports a NACK
// (ESP_ERR_INVALID_STATE), so drivers see their chip as absent instead of
// acting on zeroed registers (a zeroed BQ25896 reads as a flat battery).
static uint8_t s_i2c_bus_token;
static uint8_t s_i2c_dev_token;

esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *bus_config,
                             i2c_master_bus_handle_t *ret_bus_handle) {
    if (bus_config == nullptr || ret_bus_handle == nullptr) return ESP_ERR_INVALID_ARG;
    *ret_bus_handle = reinterpret_cast<i2c_master_bus_handle_t>(&s_i2c_bus_token);
    return ESP_OK;
}
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus_handle) { (void)bus_handle; return ESP_OK; }
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus_handle,
                                    const i2c_device_config_t *dev_config,
                                    i2c_master_dev_handle_t *ret_handle) {
    if (bus_handle == nullptr || dev_config == nullptr || ret_handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    *ret_handle = reinterpret_cast<i2c_master_dev_handle_t>(&s_i2c_dev_token);
    return ESP_OK;
}
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t handle) { (void)handle; return ESP_OK; }
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t *write_buffer,
                              size_t write_size, int xfer_timeout_ms) {
    (void)dev; (void)write_buffer; (void)write_size; (void)xfer_timeout_ms;
    return ESP_ERR_INVALID_STATE;
}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t dev, const uint8_t *write_buffer,
                                      size_t write_size, uint8_t *read_buffer, size_t read_size,
                                      int xfer_timeout_ms) {
    (void)dev; (void)write_buffer; (void)write_size; (void)xfer_timeout_ms;
    if (read_buffer != nullptr) memset(read_buffer, 0, read_size);
    return ESP_ERR_INVALID_STATE;
}
esp_err_t i2c_master_receive(i2c_master_dev_handle_t dev, uint8_t *read_buffer, size_t read_size,
                             int xfer_timeout_ms) {
    (void)dev; (void)xfer_timeout_ms;
    if (read_buffer != nullptr) memset(read_buffer, 0, read_size);
    return ESP_ERR_INVALID_STATE;
}
esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus_handle, uint16_t address,
                           int xfer_timeout_ms) {
    (void)bus_handle; (void)address; (void)xfer_timeout_ms;
    return ESP_ERR_NOT_FOUND;
}
esp_err_t i2c_master_bus_reset(i2c_master_bus_handle_t bus_handle) { (void)bus_handle; return ESP_OK; }

// ═══════════════════════════════════════════════════════════════════════════════
// SPI Stubs
// ═══════════════════════════════════════════════════════════════════════════════

esp_err_t spi_device_acquire_bus(spi_device_handle_t handle, uint32_t wait) {
    (void)handle; (void)wait; return ESP_OK;
}
void spi_device_release_bus(spi_device_handle_t handle) { (void)handle; }

esp_err_t spi_bus_initialize(spi_host_device_t host, const spi_bus_config_t *bus, int dma) {
    (void)host; (void)bus; (void)dma; return ESP_OK;
}
esp_err_t spi_bus_add_device(spi_host_device_t host, const spi_device_interface_config_t *dev, spi_device_handle_t *handle) {
    (void)host; (void)dev;
    if (handle) *handle = (void *)(uintptr_t)1; // non-NULL to pass checks
    return ESP_OK;
}
esp_err_t spi_device_transmit(spi_device_handle_t handle, const void *trans) { (void)handle; (void)trans; return ESP_OK; }
esp_err_t spi_device_polling_transmit(spi_device_handle_t handle, const void *trans) {
    (void)handle; (void)trans; return ESP_OK;
}
esp_err_t spi_bus_remove_device(spi_device_handle_t handle) { (void)handle; return ESP_OK; }

// SPI Bridge PHY — implemented after SPIBridgeChannel is declared (see below).

// ═══════════════════════════════════════════════════════════════════════════════
// RMT (Remote Control TX/RX) — opaque-handle stubs with honest degradation
// ═══════════════════════════════════════════════════════════════════════════════
namespace {

struct HleRmtHandle {
    enum class Kind { RxChannel, TxChannel } kind;
    bool enabled = false;
    rmt_rx_event_callbacks_t rx_cbs = {};
    void *rx_user_data = nullptr;
};

struct HleRmtEncoder {
    enum class Kind { Copy } kind;
};

} // namespace

esp_err_t rmt_new_rx_channel(const rmt_rx_channel_config_t *config, rmt_channel_handle_t *ret_chan) {
    if (!config || !ret_chan) return ESP_ERR_INVALID_ARG;
    *ret_chan = new HleRmtHandle{HleRmtHandle::Kind::RxChannel};
    return ESP_OK;
}
esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t *config, rmt_channel_handle_t *ret_chan) {
    if (!config || !ret_chan) return ESP_ERR_INVALID_ARG;
    *ret_chan = new HleRmtHandle{HleRmtHandle::Kind::TxChannel};
    return ESP_OK;
}
esp_err_t rmt_new_copy_encoder(const rmt_copy_encoder_config_t *config, rmt_encoder_handle_t *ret_encoder) {
    if (!config || !ret_encoder) return ESP_ERR_INVALID_ARG;
    *ret_encoder = new HleRmtEncoder{HleRmtEncoder::Kind::Copy};
    return ESP_OK;
}
esp_err_t rmt_rx_register_event_callbacks(rmt_channel_handle_t rx_chan, const rmt_rx_event_callbacks_t *cbs, void *user_data) {
    if (!rx_chan || !cbs) return ESP_ERR_INVALID_ARG;
    auto *h = static_cast<HleRmtHandle *>(rx_chan);
    if (h->kind != HleRmtHandle::Kind::RxChannel) return ESP_ERR_INVALID_ARG;
    h->rx_cbs = *cbs;
    h->rx_user_data = user_data;
    return ESP_OK;
}
esp_err_t rmt_enable(rmt_channel_handle_t chan) {
    if (!chan) return ESP_ERR_INVALID_ARG;
    static_cast<HleRmtHandle *>(chan)->enabled = true;
    return ESP_OK;
}
esp_err_t rmt_disable(rmt_channel_handle_t chan) {
    if (!chan) return ESP_ERR_INVALID_ARG;
    static_cast<HleRmtHandle *>(chan)->enabled = false;
    return ESP_OK;
}
esp_err_t rmt_receive(rmt_channel_handle_t rx_chan, rmt_symbol_word_t *buffer, size_t buffer_size, const rmt_receive_config_t *config) {
    if (!rx_chan || !buffer || !config) return ESP_ERR_INVALID_ARG;
    auto *h = static_cast<HleRmtHandle *>(rx_chan);
    if (h->kind != HleRmtHandle::Kind::RxChannel) return ESP_ERR_INVALID_ARG;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t rmt_transmit(rmt_channel_handle_t tx_chan, rmt_encoder_handle_t encoder, const void *payload, size_t payload_bytes, const rmt_transmit_config_t *config) {
    if (!tx_chan || !encoder || !payload || !config) return ESP_ERR_INVALID_ARG;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t rmt_tx_wait_all_done(rmt_channel_handle_t tx_chan, int timeout_ms) {
    if (!tx_chan) return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}
esp_err_t rmt_del_channel(rmt_channel_handle_t chan) {
    if (!chan) return ESP_ERR_INVALID_ARG;
    delete static_cast<HleRmtHandle *>(chan);
    return ESP_OK;
}
esp_err_t rmt_del_encoder(rmt_encoder_handle_t encoder) {
    if (!encoder) return ESP_ERR_INVALID_ARG;
    delete static_cast<HleRmtEncoder *>(encoder);
    return ESP_OK;
}
esp_err_t rmt_apply_carrier(rmt_channel_handle_t tx_chan, const rmt_carrier_config_t *config) {
    if (!tx_chan || !config) return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}

// ═══════════════════════════════════════════════════════════════════════════════
// SPI Bridge Channel Integration
// ═══════════════════════════════════════════════════════════════════════════════

static std::atomic<hle::SPIBridgeChannel *> s_bridge_channel{nullptr};

void hle_set_bridge_channel(hle::SPIBridgeChannel *channel) {
    s_bridge_channel.store(channel, std::memory_order_release);
}

hle::SPIBridgeChannel *hle_get_bridge_channel(void) {
    return s_bridge_channel.load(std::memory_order_acquire);
}

// ── SPI Bridge PHY — wired to in-memory channel ────────────────────────────────
// spi_bridge.c calls: transmit(cmd,NULL) → wait_irq → transmit(empty,resp)
// We track the pending command across these calls.

#include "spi_protocol.h"

static std::recursive_mutex s_phy_mutex;
static spi_header_t s_pending_cmd_header;
static uint8_t s_pending_cmd_payload[SPI_MAX_PAYLOAD];
static bool s_has_pending_cmd = false;
static uint32_t s_pending_timeout_ms = 0;
static bool s_phy_uses_irq = true;

extern "C" {

esp_err_t spi_bridge_phy_init_ex(bool setup_irq) {
    std::lock_guard<std::recursive_mutex> lock(s_phy_mutex);
    if (hle_get_bridge_channel() == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    s_has_pending_cmd = false;
    s_pending_timeout_ms = 0;
    s_phy_uses_irq = setup_irq;
    ESP_LOGI("SPI_PHY", "PHY init (%s)", setup_irq ? "IRQ" : "poll");
    return ESP_OK;
}

esp_err_t spi_bridge_phy_init(void) {
    return spi_bridge_phy_init_ex(true);
}
esp_err_t spi_bridge_phy_transmit(const uint8_t *tx_data, uint8_t *rx_data, size_t len) {
    std::lock_guard<std::recursive_mutex> lock(s_phy_mutex);
    auto *channel = hle_get_bridge_channel();
    if (channel == nullptr || channel->is_closed()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (rx_data == nullptr) {
        if (tx_data == nullptr || len < sizeof(spi_header_t)) {
            return ESP_ERR_INVALID_ARG;
        }

        const auto *header = reinterpret_cast<const spi_header_t *>(tx_data);
        if (len < sizeof(spi_header_t) + header->length) {
            return ESP_ERR_INVALID_SIZE;
        }

        memcpy(&s_pending_cmd_header, header, sizeof(spi_header_t));
        memset(s_pending_cmd_payload, 0, sizeof(s_pending_cmd_payload));
        if (header->length > 0) {
            memcpy(s_pending_cmd_payload, tx_data + sizeof(spi_header_t), header->length);
        }
        const uint16_t command_id = spi_header_cmd(header);
        s_has_pending_cmd = true;
        ESP_LOGI("SPI_PHY", "TX cmd 0x%04X len=%d",
                 static_cast<unsigned int>(command_id), header->length);
        channel->master_send_command(command_id, s_pending_cmd_payload, header->length);
        return ESP_OK;
    }

    if (!s_has_pending_cmd) {
        memset(rx_data, 0, len);
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t resp_id = 0;
    uint8_t resp_payload[SPI_MAX_PAYLOAD];
    uint8_t resp_len = 0;
    bool is_ok =
        channel->master_receive_response(resp_id, resp_payload, resp_len, s_pending_timeout_ms);
    memset(rx_data, 0, len);
    if (!is_ok) {
        if (!s_phy_uses_irq) {
            return ESP_OK;
        }
        s_has_pending_cmd = false;
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI("SPI_PHY", "RX response id=0x%04X len=%d",
             static_cast<unsigned int>(resp_id), resp_len);
    s_has_pending_cmd = false;

    const size_t total = sizeof(spi_header_t) + resp_len;
    if (len < total) {
        return ESP_ERR_INVALID_SIZE;
    }

    spi_header_t resp_header{};
    resp_header.sync = SPI_SYNC_BYTE;
    resp_header.type = SPI_TYPE_RESP;
    spi_header_set_cmd(&resp_header, resp_id);
    resp_header.length = resp_len;
    memcpy(rx_data, &resp_header, sizeof(resp_header));
    if (resp_len > 0) {
        memcpy(rx_data + sizeof(resp_header), resp_payload, resp_len);
    }
    spi_frame_seal(reinterpret_cast<spi_header_t *>(rx_data), resp_len);
    return ESP_OK;
}
esp_err_t spi_bridge_phy_wait_irq(uint32_t timeout_ms) {
    std::lock_guard<std::recursive_mutex> lock(s_phy_mutex);
    auto *channel = hle_get_bridge_channel();
    if (channel == nullptr || channel->is_closed()) {
        return ESP_ERR_INVALID_STATE;
    }

    s_pending_timeout_ms = timeout_ms;
    ESP_LOGI("SPI_PHY", "Wait IRQ %" PRIu32 " ms", timeout_ms);
    const bool is_ready = channel->master_wait_irq(timeout_ms);
    ESP_LOGI("SPI_PHY", "IRQ result=%d", is_ready);
    return is_ready ? ESP_OK : ESP_ERR_TIMEOUT;
}

} // extern "C"

static const size_t B_FRAME = SPI_FRAME_SIZE;

// SPI slave stubs — wired to in-memory channel
esp_err_t spi_slave_initialize(int host, const spi_bus_config_t *bus,
                                const spi_slave_interface_config_t *cfg, int dma) {
    (void)host; (void)bus; (void)cfg; (void)dma; return ESP_OK;
}

esp_err_t spi_slave_transmit(int host, spi_slave_transaction_t *trans, int timeout) {
    (void)host;
    (void)timeout;
    auto *channel = hle_get_bridge_channel();
    if (channel == nullptr || channel->is_closed() || trans == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (trans->tx_buffer == nullptr && trans->rx_buffer != nullptr) {
        uint16_t command_id;
        uint8_t payload[SPI_MAX_PAYLOAD];
        uint8_t payload_len;
        if (!channel->slave_wait_command(command_id, payload, payload_len)) {
            return ESP_FAIL;
        }

        auto *header = static_cast<spi_header_t *>(trans->rx_buffer);
        memset(header, 0, B_FRAME);
        header->sync = SPI_SYNC_BYTE;
        header->type = SPI_TYPE_CMD;
        spi_header_set_cmd(header, command_id);
        header->length = payload_len;
        if (payload_len > 0) {
            memcpy(reinterpret_cast<uint8_t *>(header) + sizeof(*header), payload, payload_len);
        }
        spi_frame_seal(header, payload_len);
    } else if (trans->tx_buffer != nullptr && trans->rx_buffer == nullptr) {
        const auto *header = static_cast<const spi_header_t *>(trans->tx_buffer);
        if (header->sync != SPI_SYNC_BYTE) {
            return ESP_FAIL;
        }

        const uint16_t command_id = spi_header_cmd(header);
        const auto *payload =
            reinterpret_cast<const uint8_t *>(header) + sizeof(spi_header_t);
        if (header->type == SPI_TYPE_RESP) {
            const uint8_t status = header->length > 0 ? payload[0] : 0;
            const uint8_t data_len = header->length > 0 ? header->length - 1 : 0;
            channel->slave_send_response(command_id, status, payload + 1, data_len);
            channel->slave_notify_irq();
        } else if (header->type == SPI_TYPE_STREAM) {
            channel->slave_send_stream(command_id, payload, header->length);
        } else {
            return ESP_ERR_INVALID_ARG;
        }
    } else {
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

// ═══════════════════════════════════════════════════════════════════════════════
// LED Strip Stubs
// ═══════════════════════════════════════════════════════════════════════════════

esp_err_t led_strip_new_rmt_device(const led_strip_config_t *strip_cfg,
                                    const led_strip_rmt_config_t *rmt_cfg,
                                    led_strip_handle_t *handle) {
    (void)strip_cfg; (void)rmt_cfg;
    if (handle) *handle = (void *)(uintptr_t)1;
    return ESP_OK;
}
esp_err_t led_strip_set_pixel(led_strip_handle_t handle, uint32_t index, uint32_t r, uint32_t g, uint32_t b) {
    (void)handle; (void)index; (void)r; (void)g; (void)b; return ESP_OK;
}
esp_err_t led_strip_refresh(led_strip_handle_t handle) { (void)handle; return ESP_OK; }
esp_err_t led_strip_clear(led_strip_handle_t handle) { (void)handle; return ESP_OK; }
esp_err_t led_strip_del(led_strip_handle_t handle) { (void)handle; return ESP_OK; }

// ═══════════════════════════════════════════════════════════════════════════════
// RMT (new API) stubs — implemented above
// ═══════════════════════════════════════════════════════════════════════════════

// SD card stubs
esp_err_t sdmmc_host_init(void) { return ESP_OK; }
esp_err_t sdmmc_host_init_slot(int slot, const void *config) { (void)slot; (void)config; return ESP_OK; }
void sdmmc_card_print_info(FILE *f, const sdmmc_card_t *card) {
    (void)f; (void)card; fprintf(stderr, "I [SDMMC] Mock SD card\n");
}
void sdmmc_host_deinit(void) {}

// ═══════════════════════════════════════════════════════════════════════════════
// ESP LCD Panel Stubs (wired to HLE Display)
// ═══════════════════════════════════════════════════════════════════════════════

struct HLEPanel {
    esp_lcd_panel_io_color_trans_done_cb_t on_color_trans_done;
    void *ctx;
};

esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t io) { (void)io; return ESP_OK; }
esp_err_t esp_lcd_panel_del(esp_lcd_panel_handle_t panel) { (void)panel; return ESP_OK; }

esp_err_t esp_lcd_new_panel_io_spi(int bus, const esp_lcd_panel_io_spi_config_t *cfg,
                                    esp_lcd_panel_io_handle_t *out_io) {
    (void)bus; (void)cfg;
    auto *io = new HLEPanel{};
    if (out_io) *out_io = io;
    return ESP_OK;
}

esp_err_t esp_lcd_new_panel_st7789(esp_lcd_panel_io_handle_t io,
                                    const esp_lcd_panel_dev_config_t *cfg,
                                    esp_lcd_panel_handle_t *out_panel) {
    (void)io; (void)cfg;
    if (out_panel) *out_panel = io;
    return ESP_OK;
}

esp_err_t esp_lcd_panel_reset(esp_lcd_panel_handle_t panel) { (void)panel; return ESP_OK; }
esp_err_t esp_lcd_panel_init(esp_lcd_panel_handle_t panel) { (void)panel; return ESP_OK; }
esp_err_t esp_lcd_panel_invert_color(esp_lcd_panel_handle_t panel, bool invert) { (void)panel; (void)invert; return ESP_OK; }
esp_err_t esp_lcd_panel_disp_on_off(esp_lcd_panel_handle_t panel, bool on) { (void)panel; (void)on; return ESP_OK; }
esp_err_t esp_lcd_panel_mirror(esp_lcd_panel_handle_t panel, bool mx, bool my) { (void)panel; (void)mx; (void)my; return ESP_OK; }
esp_err_t esp_lcd_panel_swap_xy(esp_lcd_panel_handle_t panel, bool swap) { (void)panel; (void)swap; return ESP_OK; }
esp_err_t esp_lcd_panel_set_gap(esp_lcd_panel_handle_t panel, int x, int y) { (void)panel; (void)x; (void)y; return ESP_OK; }

esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel, int x0, int y0, int x1, int y1,
                                     const void *data) {
    (void)panel;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > hle::LCD_H_RES) x1 = hle::LCD_H_RES;
    if (y1 > hle::LCD_V_RES) y1 = hle::LCD_V_RES;
    hle::Display::instance().draw_bitmap(x0, y0, x1, y1, static_cast<const uint16_t *>(data));

    // Notify LVGL flush ready
    auto *io = static_cast<HLEPanel *>(panel);
    if (io && io->on_color_trans_done) {
        io->on_color_trans_done(io, nullptr, io->ctx);
    }
    return ESP_OK;
}

esp_err_t esp_lcd_panel_io_register_event_callbacks(esp_lcd_panel_io_handle_t io,
    const esp_lcd_panel_io_callbacks_t *cbs, void *ctx) {
    auto *p = static_cast<HLEPanel *>(io);
    if (p && cbs) {
        p->on_color_trans_done = cbs->on_color_trans_done;
        p->ctx = ctx;
    }
    return ESP_OK;
}

// ═══════════════════════════════════════════════════════════════════════════════
// VFS / Storage stubs (mount to host tmpdir)
// ═══════════════════════════════════════════════════════════════════════════════

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>

#include "vfs_core.h"

static std::string s_base_path = "/tmp/hle_storage";

void hle_set_storage_path(const char *path) {
    if (path != nullptr && path[0] != '\0') {
        s_base_path = path;
    }
}

const char *hle_get_storage_path(void) {
    return s_base_path.c_str();
}

static std::string hle_translate_path(const char *path) {
    if (!path || path[0] == '\0') return {};

    const std::string src(path);
    if (src.rfind("/sdcard", 0) == 0) {
        return s_base_path + src;
    }

    if (src.rfind("/assets", 0) == 0) {
#ifdef HLE_ASSETS_DIR
        return std::string(HLE_ASSETS_DIR) + src.substr(sizeof("/assets") - 1);
#else
        return s_base_path + src;
#endif
    }

    return src;
}

extern "C" FILE *__real_fopen(const char *path, const char *mode);
extern "C" int __real_stat(const char *path, struct stat *st);
extern "C" DIR *__real_opendir(const char *path);
extern "C" int __real_mkdir(const char *path, mode_t mode);
extern "C" int __real_access(const char *path, int mode);
extern "C" int __real_remove(const char *path);
extern "C" int __real_rename(const char *old_path, const char *new_path);
extern "C" int __real_unlink(const char *path);

extern "C" FILE *__wrap_fopen(const char *path, const char *mode) {
    const std::string translated = hle_translate_path(path);
    return __real_fopen(translated.c_str(), mode);
}

extern "C" int __wrap_stat(const char *path, struct stat *st) {
    const std::string translated = hle_translate_path(path);
    return __real_stat(translated.c_str(), st);
}

extern "C" DIR *__wrap_opendir(const char *path) {
    const std::string translated = hle_translate_path(path);
    return __real_opendir(translated.c_str());
}

extern "C" int __wrap_mkdir(const char *path, mode_t mode) {
    const std::string translated = hle_translate_path(path);
    return __real_mkdir(translated.c_str(), mode);
}

extern "C" int __wrap_access(const char *path, int mode) {
    const std::string translated = hle_translate_path(path);
    return __real_access(translated.c_str(), mode);
}

extern "C" int __wrap_remove(const char *path) {
    const std::string translated = hle_translate_path(path);
    return __real_remove(translated.c_str());
}

extern "C" int __wrap_rename(const char *old_path, const char *new_path) {
    const std::string translated_old = hle_translate_path(old_path);
    const std::string translated_new = hle_translate_path(new_path);
    return __real_rename(translated_old.c_str(), translated_new.c_str());
}

extern "C" int __wrap_unlink(const char *path) {
    const std::string translated = hle_translate_path(path);
    return __real_unlink(translated.c_str());
}

// Host backend for /sdcard: the POSIX calls vfs_sdcard.c makes on the card,
// applied to the translated host path.
static void hle_sd_fill_stat(const struct stat &native, const char *name, vfs_stat_t *st) {
    strncpy(st->name, name, VFS_MAX_NAME - 1);
    st->name[VFS_MAX_NAME - 1] = '\0';
    st->type = S_ISDIR(native.st_mode) ? VFS_TYPE_DIR : VFS_TYPE_FILE;
    st->size = native.st_size;
    st->mtime = native.st_mtime;
    st->ctime = native.st_ctime;
    st->is_hidden = false;
    st->is_readonly = false;
}

static esp_err_t hle_sd_init(void) { return ESP_OK; }
static esp_err_t hle_sd_deinit(void) { return ESP_OK; }
static bool hle_sd_is_mounted(void) { return true; }
static vfs_fd_t hle_sd_open(const char *path, int flags, int mode) {
    return open(hle_translate_path(path).c_str(), flags, mode);
}
static ssize_t hle_sd_read(vfs_fd_t fd, void *buf, size_t size) { return read(fd, buf, size); }
static ssize_t hle_sd_write(vfs_fd_t fd, const void *buf, size_t size) {
    return write(fd, buf, size);
}
static off_t hle_sd_lseek(vfs_fd_t fd, off_t offset, int whence) {
    return lseek(fd, offset, whence);
}
static esp_err_t hle_sd_close(vfs_fd_t fd) { return close(fd) == 0 ? ESP_OK : ESP_FAIL; }
static esp_err_t hle_sd_fsync(vfs_fd_t fd) { return fsync(fd) == 0 ? ESP_OK : ESP_FAIL; }
static esp_err_t hle_sd_stat(const char *path, vfs_stat_t *st) {
    struct stat native;
    if (__real_stat(hle_translate_path(path).c_str(), &native) != 0) return ESP_FAIL;
    const char *name = strrchr(path, '/');
    hle_sd_fill_stat(native, name ? name + 1 : path, st);
    return ESP_OK;
}
static esp_err_t hle_sd_fstat(vfs_fd_t fd, vfs_stat_t *st) {
    struct stat native;
    if (fstat(fd, &native) != 0) return ESP_FAIL;
    hle_sd_fill_stat(native, "", st);
    return ESP_OK;
}
static esp_err_t hle_sd_rename(const char *old_path, const char *new_path) {
    return __real_rename(hle_translate_path(old_path).c_str(),
                         hle_translate_path(new_path).c_str()) == 0 ? ESP_OK : ESP_FAIL;
}
static esp_err_t hle_sd_unlink(const char *path) {
    return __real_unlink(hle_translate_path(path).c_str()) == 0 ? ESP_OK : ESP_FAIL;
}
static esp_err_t hle_sd_truncate(const char *path, off_t length) {
    return truncate(hle_translate_path(path).c_str(), length) == 0 ? ESP_OK : ESP_FAIL;
}
static esp_err_t hle_sd_mkdir(const char *path, int mode) {
    return __real_mkdir(hle_translate_path(path).c_str(), mode) == 0 ? ESP_OK : ESP_FAIL;
}
static esp_err_t hle_sd_rmdir(const char *path) {
    return rmdir(hle_translate_path(path).c_str()) == 0 ? ESP_OK : ESP_FAIL;
}
static vfs_dir_t hle_sd_opendir(const char *path) {
    return reinterpret_cast<vfs_dir_t>(__real_opendir(hle_translate_path(path).c_str()));
}
static esp_err_t hle_sd_readdir(vfs_dir_t dir, vfs_stat_t *entry) {
    struct dirent *ent = readdir(reinterpret_cast<DIR *>(dir));
    if (ent == nullptr) return ESP_ERR_NOT_FOUND;
    strncpy(entry->name, ent->d_name, VFS_MAX_NAME - 1);
    entry->name[VFS_MAX_NAME - 1] = '\0';
    entry->type = ent->d_type == DT_DIR ? VFS_TYPE_DIR : VFS_TYPE_FILE;
    entry->size = 0;
    return ESP_OK;
}
static esp_err_t hle_sd_closedir(vfs_dir_t dir) {
    return closedir(reinterpret_cast<DIR *>(dir)) == 0 ? ESP_OK : ESP_FAIL;
}
static esp_err_t hle_sd_statvfs(vfs_statvfs_t *stat) {
    struct statvfs native;
    if (statvfs(hle_translate_path("/sdcard").c_str(), &native) != 0) return ESP_FAIL;
    stat->block_size = static_cast<uint32_t>(native.f_frsize);
    stat->total_blocks = static_cast<uint32_t>(native.f_blocks);
    stat->free_blocks = static_cast<uint32_t>(native.f_bavail);
    stat->total_bytes = static_cast<uint64_t>(native.f_blocks) * native.f_frsize;
    stat->free_bytes = static_cast<uint64_t>(native.f_bavail) * native.f_frsize;
    stat->used_bytes = stat->total_bytes - stat->free_bytes;
    return ESP_OK;
}

static const vfs_backend_ops_t s_hle_sd_ops = {
    .init = hle_sd_init,
    .deinit = hle_sd_deinit,
    .is_mounted = hle_sd_is_mounted,
    .open = hle_sd_open,
    .read = hle_sd_read,
    .write = hle_sd_write,
    .lseek = hle_sd_lseek,
    .close = hle_sd_close,
    .fsync = hle_sd_fsync,
    .stat = hle_sd_stat,
    .fstat = hle_sd_fstat,
    .rename = hle_sd_rename,
    .unlink = hle_sd_unlink,
    .truncate = hle_sd_truncate,
    .mkdir = hle_sd_mkdir,
    .rmdir = hle_sd_rmdir,
    .opendir = hle_sd_opendir,
    .readdir = hle_sd_readdir,
    .closedir = hle_sd_closedir,
    .statvfs = hle_sd_statvfs,
};

extern "C" const vfs_backend_ops_t *hle_sd_backend_ops(void) { return &s_hle_sd_ops; }

extern "C" {

esp_err_t esp_vfs_fat_sdmmc_mount(const char *base_path, const sdmmc_host_t *host, const void *slot,
                                   const void *mount_conf, sdmmc_card_t **out_card) {
    (void)host; (void)slot; (void)mount_conf;
    std::string dir = s_base_path + (base_path ? base_path : "/sdcard");
    mkdir(dir.c_str(), 0755);
    fprintf(stderr, "I [VFS] Mounted HLE storage at %s\n", dir.c_str());
    if (out_card) *out_card = (sdmmc_card_t *)0x1;
    return ESP_OK;
}

esp_err_t esp_vfs_fat_sdmmc_unmount(void) { return ESP_OK; }
esp_err_t esp_vfs_fat_sdcard_unmount(const char *base_path, sdmmc_card_t *card) {
    (void)base_path; (void)card; return ESP_OK;
}

esp_err_t esp_vfs_fat_spiflash_mount(const char *base, const char *part_label, const void *mount, void *wl_handle) {
    (void)base; (void)part_label; (void)mount; (void)wl_handle;
    std::string dir = s_base_path + "/assets";
    mkdir(dir.c_str(), 0755);
    return ESP_OK;
}

esp_err_t esp_vfs_fat_spiflash_unmount(const char *base, void *wl_handle) {
    (void)base; (void)wl_handle; return ESP_OK;
}

} // extern "C"

// LittleFS stubs
static bool s_littlefs_mounted = false;

esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t *conf) {
    if (conf && conf->base_path) {
        std::string dir = s_base_path + conf->base_path;
        mkdir(dir.c_str(), 0755);
        fprintf(stderr, "I [LFS] Registered HLE LittleFS at %s\n", conf->base_path);
    }
    s_littlefs_mounted = true;
    return ESP_OK;
}
esp_err_t esp_vfs_littlefs_unregister(const char *label) {
    (void)label;
    s_littlefs_mounted = false;
    return ESP_OK;
}
bool esp_littlefs_mounted(const char *partition_label) {
    (void)partition_label;
    return s_littlefs_mounted;
}

// Partition stubs
esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label) {
    (void)type; (void)subtype; (void)label;
    static esp_partition_t p = {0, 8*1024*1024, "storage"};
    return &p;
}
const esp_partition_t *esp_ota_get_running_partition(void) { return nullptr; }
void esp_partition_get_sha256(const esp_partition_t *p, uint8_t *sha) { (void)p; memset(sha, 0, 32); }
