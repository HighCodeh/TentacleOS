#include <cstdio>
#include <cstdlib>
#include <thread>
#include <chrono>

extern "C" {
#include "esp_log.h"
}
#include "hle/sdl_renderer.h"
#include "hle/hle_display.h"
#include "hle/spi_bridge_channel.h"
#include "hle/hle_kernel.h"

#include <lvgl.h>

static const char *TAG = "MAIN";

static uint32_t lv_get_tick(void) {
    static const auto s_start = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(now - s_start).count();
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    const char *snapshot_path = std::getenv("HLE_SNAPSHOT_PATH");
    const char *snapshot_ms_env = std::getenv("HLE_SNAPSHOT_MS");
    const uint32_t snapshot_ms =
        snapshot_ms_env ? static_cast<uint32_t>(std::strtoul(snapshot_ms_env, nullptr, 10)) : 0;
    bool snapshot_taken = false;
    const char *storage_path = std::getenv("HLE_STORAGE_PATH");
    if (storage_path != nullptr && storage_path[0] != '\0') {
        hle_set_storage_path(storage_path);
    }
    int exit_code = 0;

    hle::SDLRenderer renderer;
    if (!renderer.init()) {
        fprintf(stderr, "Failed to initialize SDL2\n");
        return 1;
    }

    lv_init();
    lv_tick_set_cb(lv_get_tick);

    // Create bridge channel and hand it to kernel + shims
    hle::SPIBridgeChannel channel;
    hle_set_bridge_channel(&channel);

    // Boot firmware
    hle_kernel_init();

    ESP_LOGI(TAG, "=== TentacleOS HLE running ===");

    bool quit = false;
    while (!quit) {
        uint8_t keys_held;
        renderer.handle_events(keys_held, quit);
        hle_set_button_mask(keys_held);
        renderer.render();

        if (!snapshot_taken && snapshot_path != nullptr && lv_get_tick() >= snapshot_ms) {
            if (!hle::Display::instance().save_ppm(snapshot_path)) {
                ESP_LOGE(TAG, "Failed to save snapshot to %s", snapshot_path);
                exit_code = 1;
            }
            snapshot_taken = true;
            quit = true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 fps
    }

    hle_kernel_shutdown();

    renderer.shutdown();
    ESP_LOGI(TAG, "Shutdown complete");
    return exit_code;
}
