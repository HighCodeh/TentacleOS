#include <array>

#include <gtest/gtest.h>
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "hle/hle_display.h"

TEST(HLEDummyTest, BuildSystemWorks) {
    EXPECT_EQ(ESP_OK, 0);
    EXPECT_NE(ESP_ERR_NO_MEM, ESP_OK);
}

class HLENVS : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(nvs_flash_erase(), ESP_OK);
        ASSERT_EQ(nvs_flash_init(), ESP_OK);
    }
};

TEST_F(HLENVS, ScalarRoundTrip) {
    nvs_handle_t handle;
    ASSERT_EQ(nvs_open("test", NVS_READWRITE, &handle), ESP_OK);

    ASSERT_EQ(nvs_set_i32(handle, "answer", 42), ESP_OK);
    ASSERT_EQ(nvs_commit(handle), ESP_OK);

    int32_t value = 0;
    ASSERT_EQ(nvs_get_i32(handle, "answer", &value), ESP_OK);
    EXPECT_EQ(value, 42);
    nvs_close(handle);
}

TEST_F(HLENVS, SupportsTwoPassStringReads) {
    nvs_handle_t handle;
    ASSERT_EQ(nvs_open("test", NVS_READWRITE, &handle), ESP_OK);
    ASSERT_EQ(nvs_set_str(handle, "greeting", "hello"), ESP_OK);

    size_t required_length = 0;
    ASSERT_EQ(nvs_get_str(handle, "greeting", nullptr, &required_length), ESP_OK);
    ASSERT_EQ(required_length, 6u);

    char too_small[4] = {'x', 'x', 'x', '\0'};
    size_t too_small_length = sizeof(too_small);
    EXPECT_EQ(nvs_get_str(handle, "greeting", too_small, &too_small_length),
              ESP_ERR_NVS_INVALID_LENGTH);
    EXPECT_EQ(too_small_length, required_length);
    EXPECT_STREQ(too_small, "xxx");

    char value[6] = {};
    size_t value_length = sizeof(value);
    ASSERT_EQ(nvs_get_str(handle, "greeting", value, &value_length), ESP_OK);
    EXPECT_EQ(value_length, required_length);
    EXPECT_STREQ(value, "hello");
    nvs_close(handle);
}

TEST_F(HLENVS, EnforcesTypesModesAndHandleLifetime) {
    nvs_handle_t missing_handle = 0;
    EXPECT_EQ(nvs_open("missing", NVS_READONLY, &missing_handle), ESP_ERR_NVS_NOT_FOUND);

    nvs_handle_t writer;
    ASSERT_EQ(nvs_open("test", NVS_READWRITE, &writer), ESP_OK);
    ASSERT_EQ(nvs_set_u32(writer, "typed", 42), ESP_OK);
    nvs_close(writer);

    nvs_handle_t reader;
    ASSERT_EQ(nvs_open("test", NVS_READONLY, &reader), ESP_OK);
    int32_t signed_value = 0;
    EXPECT_EQ(nvs_get_i32(reader, "typed", &signed_value), ESP_ERR_NVS_TYPE_MISMATCH);
    EXPECT_EQ(nvs_set_u32(reader, "typed", 7), ESP_ERR_NVS_READ_ONLY);
    nvs_close(reader);

    uint32_t value = 0;
    EXPECT_EQ(nvs_get_u32(reader, "typed", &value), ESP_ERR_NVS_INVALID_HANDLE);
    EXPECT_EQ(nvs_commit(reader), ESP_ERR_NVS_INVALID_HANDLE);
}

TEST(HLEDisplay, ClipsPixelsAtRowBoundaries) {
    auto &display = hle::Display::instance();
    display.fill_screen(0);

    std::array<uint16_t, hle::LCD_H_RES * hle::LCD_V_RES> pixels{};
    ASSERT_TRUE(display.copy_pixels_if_dirty(pixels.data(),
                                             hle::LCD_H_RES * sizeof(uint16_t)));

    const uint16_t source[] = {0x1111, 0x2222, 0x3333};
    display.draw_bitmap(hle::LCD_H_RES - 1, 0, hle::LCD_H_RES + 2, 1, source);
    ASSERT_TRUE(display.copy_pixels_if_dirty(pixels.data(),
                                             hle::LCD_H_RES * sizeof(uint16_t)));

    EXPECT_EQ(pixels[hle::LCD_H_RES - 1], source[0]);
    EXPECT_EQ(pixels[hle::LCD_H_RES], 0);
    EXPECT_FALSE(display.copy_pixels_if_dirty(pixels.data(),
                                              hle::LCD_H_RES * sizeof(uint16_t)));
}

TEST(HLEDummyTest, FreeRTOSMutex) {
    SemaphoreHandle_t mtx = xSemaphoreCreateMutex();
    ASSERT_NE(mtx, nullptr);
    EXPECT_EQ(xSemaphoreTake(mtx, 100), pdTRUE);
    EXPECT_EQ(xSemaphoreGive(mtx), pdTRUE);
    vSemaphoreDelete(mtx);
}

TEST(HLEDummyTest, FreeRTOSQueue) {
    QueueHandle_t q = xQueueCreate(4, sizeof(int32_t));
    ASSERT_NE(q, nullptr);

    int32_t send_val = 99;
    EXPECT_EQ(xQueueSend(q, &send_val, 0), pdTRUE);

    int32_t recv_val = 0;
    EXPECT_EQ(xQueueReceive(q, &recv_val, 100), pdTRUE);
    EXPECT_EQ(recv_val, 99);

    vQueueDelete(q);
}

TEST(HLEDummyTest, FreeRTOSTask) {
    static volatile bool ran = false;

    auto func = [](void *arg) {
        (void)arg;
        ran = true;
        vTaskDelay(10);
        vTaskDelete(nullptr);
    };

    TaskHandle_t handle;
    BaseType_t ret = xTaskCreatePinnedToCore(func, "dummy", 4096, nullptr, 1, &handle, 0);
    EXPECT_EQ(ret, pdPASS);

    // Give it time to run
    vTaskDelay(100);
    EXPECT_TRUE(ran);
}
