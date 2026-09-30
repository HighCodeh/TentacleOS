#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <set>
#include <string>
#include <thread>

extern "C" {
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
}

static void count_callback(void *arg) { static_cast<std::atomic<int> *>(arg)->fetch_add(1); }

static esp_timer_handle_t make_counting_timer(std::atomic<int> *count) {
    esp_timer_create_args_t args{};
    args.name = "test";
    args.callback = count_callback;
    args.arg = count;
    esp_timer_handle_t timer = nullptr;
    EXPECT_EQ(esp_timer_create(&args, &timer), ESP_OK);
    return timer;
}

TEST(EspTimer, PeriodicFiresUntilStopped) {
    std::atomic<int> count{0};
    esp_timer_handle_t timer = make_counting_timer(&count);

    ASSERT_EQ(esp_timer_start_periodic(timer, 5000), ESP_OK);
    EXPECT_EQ(esp_timer_start_periodic(timer, 5000), ESP_ERR_INVALID_STATE);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_EQ(esp_timer_stop(timer), ESP_OK);
    const int fired = count.load();
    EXPECT_GE(fired, 5);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_LE(count.load(), fired + 1);
    EXPECT_EQ(esp_timer_stop(timer), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(esp_timer_delete(timer), ESP_OK);
}

TEST(EspTimer, OneShotFiresOnce) {
    std::atomic<int> count{0};
    esp_timer_handle_t timer = make_counting_timer(&count);

    ASSERT_EQ(esp_timer_start_once(timer, 10000), ESP_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(count.load(), 1);
    EXPECT_EQ(esp_timer_delete(timer), ESP_OK);
}

TEST(EspTimer, RunningTimerCannotBeDeleted) {
    std::atomic<int> count{0};
    esp_timer_handle_t timer = make_counting_timer(&count);

    ASSERT_EQ(esp_timer_start_periodic(timer, 1000000), ESP_OK);
    EXPECT_EQ(esp_timer_delete(timer), ESP_ERR_INVALID_STATE);
    ASSERT_EQ(esp_timer_stop(timer), ESP_OK);
    EXPECT_EQ(esp_timer_delete(timer), ESP_OK);
}

struct NotifyContext {
    std::atomic<TaskHandle_t> self{nullptr};
    std::atomic<uint32_t> taken{0};
};

static void notify_waiter_task(void *arg) {
    auto *ctx = static_cast<NotifyContext *>(arg);
    ctx->self.store(xTaskGetCurrentTaskHandle());
    ctx->taken.store(ulTaskNotifyTake(pdTRUE, portMAX_DELAY));
    vTaskDelete(nullptr);
}

TEST(TaskNotify, GiveWakesBlockedTask) {
    NotifyContext ctx;
    TaskHandle_t handle = nullptr;
    ASSERT_EQ(xTaskCreate(notify_waiter_task, "waiter", 2048, &ctx, 1, &handle), pdPASS);

    for (int i = 0; i < 100 && ctx.self.load() == nullptr; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_EQ(ctx.self.load(), handle);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_EQ(ctx.taken.load(), 0u);

    ASSERT_EQ(xTaskNotifyGive(handle), pdPASS);
    for (int i = 0; i < 100 && ctx.taken.load() == 0; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_EQ(ctx.taken.load(), 1u);
}

TEST(TaskNotify, TakeTimesOutWithoutGive) {
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20)), 0u);
    EXPECT_GE(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(20));
}

TEST(Queue, OverwriteReplacesAndResetEmpties) {
    QueueHandle_t queue = xQueueCreate(1, sizeof(int));
    ASSERT_NE(queue, nullptr);
    const int first = 1;
    const int second = 2;
    int out = 0;

    ASSERT_EQ(xQueueOverwrite(queue, &first), pdPASS);
    ASSERT_EQ(xQueueOverwrite(queue, &second), pdPASS);
    ASSERT_EQ(xQueueReceive(queue, &out, 0), pdTRUE);
    EXPECT_EQ(out, second);

    ASSERT_EQ(xQueueSend(queue, &first, 0), pdTRUE);
    ASSERT_EQ(xQueueReset(queue), pdPASS);
    EXPECT_EQ(xQueueReceive(queue, &out, 0), pdFALSE);
    vQueueDelete(queue);
}

TEST(NvsIterator, ListsMatchingEntries) {
    ASSERT_EQ(nvs_flash_erase(), ESP_OK);
    ASSERT_EQ(nvs_flash_init(), ESP_OK);
    nvs_handle_t grants;
    nvs_handle_t other;
    ASSERT_EQ(nvs_open("grants", NVS_READWRITE, &grants), ESP_OK);
    ASSERT_EQ(nvs_open("other", NVS_READWRITE, &other), ESP_OK);
    ASSERT_EQ(nvs_set_u32(grants, "app_a", 1), ESP_OK);
    ASSERT_EQ(nvs_set_u32(grants, "app_b", 2), ESP_OK);
    ASSERT_EQ(nvs_set_str(grants, "note", "x"), ESP_OK);
    ASSERT_EQ(nvs_set_u32(other, "app_c", 3), ESP_OK);

    nvs_iterator_t it = nullptr;
    std::set<std::string> keys;
    esp_err_t err = nvs_entry_find("nvs", "grants", NVS_TYPE_U32, &it);
    while (err == ESP_OK) {
        nvs_entry_info_t info;
        ASSERT_EQ(nvs_entry_info(it, &info), ESP_OK);
        EXPECT_STREQ(info.namespace_name, "grants");
        EXPECT_EQ(info.type, NVS_TYPE_U32);
        keys.insert(info.key);
        err = nvs_entry_next(&it);
    }
    EXPECT_EQ(err, ESP_ERR_NVS_NOT_FOUND);
    EXPECT_EQ(it, nullptr);
    EXPECT_EQ(keys, (std::set<std::string>{"app_a", "app_b"}));

    int all = 0;
    for (err = nvs_entry_find("nvs", nullptr, NVS_TYPE_ANY, &it); err == ESP_OK;
         err = nvs_entry_next(&it)) {
        all++;
    }
    EXPECT_EQ(all, 4);

    EXPECT_EQ(nvs_entry_find("nvs", "missing", NVS_TYPE_ANY, &it), ESP_ERR_NVS_NOT_FOUND);
    EXPECT_EQ(it, nullptr);
    nvs_release_iterator(it);

    nvs_close(grants);
    nvs_close(other);
}
