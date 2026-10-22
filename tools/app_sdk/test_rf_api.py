#!/usr/bin/env python3
"""Compile production RF ABI wrappers with an explicit broker fixture."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SERVICE = ROOT / "firmware_p4/components/Service"


def main():
    with tempfile.TemporaryDirectory(prefix="tos-rf-abi-") as temporary:
        directory = Path(temporary)
        (directory / "esp_err.h").write_text('typedef int esp_err_t;\n')
        (directory / "esp_timer.h").write_text(
            '#include <stdint.h>\nint64_t esp_timer_get_time(void);\n')
        (directory / "esp_log.h").write_text('#define ESP_LOGW(tag, ...) ((void)(tag))\n')
        (directory / "esp_rom_crc.h").write_text('#include <stdint.h>\nuint16_t esp_rom_crc16_le(uint16_t, const uint8_t *, uint32_t);\n')
        executable = directory / "test"
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra",
                        "-Werror", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        "-I", str(directory), "-I", str(SERVICE / "app_runtime/include"),
                        "-I", str(SERVICE / "host_link/include"),
                        "-I", str(SERVICE / "spi_bridge/include"),
                        str(SERVICE / "app_runtime/tos_api_rf.c"),
                        str(ROOT / "tools/app_sdk/tests/test_rf_api.c"), "-o", str(executable)],
                       check=True, timeout=60)
        subprocess.run([str(executable)], check=True, timeout=30)
        (directory / "freertos").mkdir()
        (directory / "esp_err.h").write_text(
            "#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n"
            "#define ESP_ERR_INVALID_ARG 0x102\n#define ESP_ERR_INVALID_STATE 0x103\n"
            "#define ESP_ERR_INVALID_SIZE 0x104\n#define ESP_ERR_TIMEOUT 0x107\n"
            "#define ESP_ERR_NO_MEM 0x101\n#define ESP_ERR_NOT_FOUND 0x105\n")
        (directory / "esp_log.h").write_text('#define ESP_LOGE(tag, ...) ((void)(tag))\n')
        (directory / "freertos/FreeRTOS.h").write_text(
            '#pragma once\n#include <stdint.h>\n#include <stdbool.h>\n'
            '#define pdTRUE 1\n#define portMAX_DELAY UINT32_MAX\n'
            '#define pdMS_TO_TICKS(ms) (ms)\n')
        (directory / "freertos/semphr.h").write_text(
            '#pragma once\n#include <pthread.h>\n#include <stdint.h>\n'
            'typedef struct { pthread_mutex_t lock; pthread_cond_t ready; unsigned count; } fake_sem_t;\n'
            'typedef fake_sem_t *SemaphoreHandle_t;\n'
            'SemaphoreHandle_t xSemaphoreCreateMutex(void);\n'
            'SemaphoreHandle_t xSemaphoreCreateBinary(void);\n'
            'int xSemaphoreTake(SemaphoreHandle_t, uint32_t);\n'
            'int xSemaphoreGive(SemaphoreHandle_t);\n')
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra",
                        "-Werror", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        "-pthread", "-I", str(directory), "-I", str(SERVICE / "host_link/include"),
                        "-I", str(SERVICE / "spi_bridge/include"),
                        str(SERVICE / "host_link/host_link_local_rf.c"),
                        str(ROOT / "tools/app_sdk/tests/test_local_rf.c"), "-o", str(executable)],
                       check=True, timeout=60)
        subprocess.run([str(executable)], check=True, timeout=30)


        core = (SERVICE / "host_link/host_link.c").read_text()
        def function(signature):
            start = core.index(signature)
            opening = core.index("{", start)
            depth, end = 1, opening + 1
            while depth:
                depth += (core[end] == "{") - (core[end] == "}")
                end += 1
            return core[start:end]
        start = core.index("typedef struct {")
        end = core.index("} rx_item_t;", start) + len("} rx_item_t;")
        routing = (ROOT / "tools/app_sdk/tests/test_local_rf_routing.c").read_text()
        routing = routing.replace("/* RX_ITEM */", core[start:end])
        routing = routing.replace("/* FEED */", function("bool host_link_feed("))
        routing = routing.replace("/* PUMP */", function("static bool pump(TickType_t wait) {"))
        (directory / "routing.c").write_text(routing)
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra",
                        "-Werror", "-fsanitize=address,undefined", str(directory / "routing.c"),
                        "-o", str(executable)], check=True, timeout=60)
        subprocess.run([str(executable)], check=True, timeout=30)
        manager = (SERVICE / "app_runtime/tos_app_mgr.c").read_text()
        start = manager.index("static void cleanup_current_app(")
        end = manager.index("\nstatic void app_task", start)
        (directory / "cleanup.c").write_text(
            '#include <assert.h>\n#include <stdbool.h>\n'
            'typedef struct { int unused; } app_slot_t;\n'
            '#define APP_CLEANUP_RETRY_MS 20\n#define pdMS_TO_TICKS(ms) (ms)\n'
            'static unsigned attempts, delays, finishes;\n'
            'static bool claim_cleanup(app_slot_t *app) { (void)app; return ++attempts>2; }\n'
            'static void vTaskDelay(int ticks) { assert(ticks==20 && finishes==0); ++delays; }\n'
            'static void finish_slot(app_slot_t *app) { (void)app; ++finishes; }\n'
            + manager[start:end] +
            'int main(void) { app_slot_t app={0}; cleanup_current_app(&app);\n'
            'assert(attempts==3 && delays==2 && finishes==1); return 0; }\n')
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra",
                        "-Werror", "-fsanitize=address,undefined", str(directory / "cleanup.c"),
                        "-o", str(executable)], check=True, timeout=60)
        subprocess.run([str(executable)], check=True, timeout=30)
        kill = manager[manager.index("esp_err_t tos_app_mgr_stop"):]
        assert kill.index("host_local_rf_lock") < kill.index("spi_bridge_lock") < kill.index("vTaskDelete")
        assert "host_local_rf_unlock();\n        ESP_LOGE(TAG, \"bridge busy" in kill
        def manager_function(signature):
            start=manager.index(signature);opening=manager.index('{',start);depth=1;end=opening+1
            while depth:
                depth+=(manager[end]=='{')-(manager[end]=='}');end+=1
            return manager[start:end]
        forced=(ROOT/'tools/app_sdk/tests/test_force_stop.c').read_text()
        forced=forced.replace('/* MANAGER */',manager_function('static esp_err_t pause_for_cleanup(')+'\n'+manager_function('esp_err_t tos_app_mgr_stop('))
        (directory/'forced.c').write_text(forced)
        subprocess.run([*shlex.split(os.environ.get('CC','cc')),'-std=c11','-Wall','-Wextra',
                        '-Werror','-fsanitize=address,undefined','-I',str(SERVICE/'app_runtime/include'),
                        str(directory/'forced.c'),'-o',str(executable)],check=True)
        subprocess.run([str(executable)],check=True,timeout=30)
        print("PASS: natural exit retries a released kill claim instead of remaining suspended")
    subprocess.run([sys.executable, str(ROOT / "tools/nfc_api/test_sessions.py")], check=True)
    subprocess.run([sys.executable, str(ROOT / "tools/nfc_api/test_startup.py")], check=True)


if __name__ == "__main__":
    main()
