#!/usr/bin/env python3
# Copyright (c) 2025 HIGH CODE LLC
#
# TentacleOS is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# TentacleOS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

'''Run the production P4 client and C5 backend with a fault-injected SPI/RMT HAL.'''
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/board_control"))
from test_board_control import MOCKS, NOTICE

MOCKS = dict(MOCKS)
MOCKS["esp_err.h"] = MOCKS["esp_err.h"].replace("ESP_ERR_NOT_FINISHED", "ESP_ERR_INVALID_SIZE, ESP_ERR_INVALID_CRC, ESP_ERR_NOT_FOUND, ESP_ERR_NOT_FINISHED") + '\nconst char *esp_err_to_name(esp_err_t);\n'
MOCKS["esp_log.h"] += '#define ESP_LOGI(tag, ...) ((void)(tag))\n#define ESP_LOGD(tag, ...) ((void)(tag))\n'
MOCKS["freertos/task.h"] = MOCKS["freertos/task.h"].replace('*h = (void *)1;', 'if (h) { *h = (void *)1; }') + '\nvoid vTaskDelay(unsigned ticks);\nTaskHandle_t xTaskGetCurrentTaskHandle(void);\n'
MOCKS["sys_prio.h"] += '#define SYS_PRIO_SERVICE_HI 5\n#define SYS_CORE_MAIN 0\n'
MOCKS["pin_def.h"] = '#define GPIO_IR_TX_PIN 6\n#define GPIO_IR_RX_PIN 10\n'
MOCKS["soc/soc_caps.h"] = '#define SOC_RMT_MEM_WORDS_PER_CHANNEL 48\n'
MOCKS["driver/gpio.h"] = '''#pragma once
#include "esp_err.h"
#define GPIO_MODE_OUTPUT 1
esp_err_t gpio_set_level(int pin, int level);
esp_err_t gpio_set_direction(int pin, int mode);
'''
MOCKS["driver/rmt_types.h"] = (ROOT / "tools/ir_api/fixtures/driver/rmt_types.h").read_text()
MOCKS["driver/rmt_tx.h"] = '''#pragma once
#include "esp_err.h"
#include "driver/rmt_types.h"
#define RMT_CLK_SRC_DEFAULT 0
typedef void *rmt_channel_handle_t;
typedef void *rmt_encoder_handle_t;
typedef struct { int gpio_num, clk_src, resolution_hz, mem_block_symbols, trans_queue_depth; } rmt_tx_channel_config_t;
typedef struct { uint32_t frequency_hz; float duty_cycle; } rmt_carrier_config_t;
typedef struct { int loop_count; struct { unsigned eot_level:1; } flags; } rmt_transmit_config_t;
esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t *, rmt_channel_handle_t *);
esp_err_t rmt_apply_carrier(rmt_channel_handle_t, const rmt_carrier_config_t *);
esp_err_t rmt_enable(rmt_channel_handle_t);
esp_err_t rmt_disable(rmt_channel_handle_t);
esp_err_t rmt_del_channel(rmt_channel_handle_t);
esp_err_t rmt_transmit(rmt_channel_handle_t, rmt_encoder_handle_t, const void *, size_t, const rmt_transmit_config_t *);
esp_err_t rmt_tx_wait_all_done(rmt_channel_handle_t, int);
'''
MOCKS["driver/rmt_encoder.h"] = '''#pragma once
#include "driver/rmt_tx.h"
typedef struct {} rmt_copy_encoder_config_t;
esp_err_t rmt_new_copy_encoder(const rmt_copy_encoder_config_t *, rmt_encoder_handle_t *);
esp_err_t rmt_del_encoder(rmt_encoder_handle_t);
'''
MOCKS["driver/rmt_rx.h"] = '''#pragma once
#include "driver/rmt_tx.h"
typedef struct { int gpio_num, clk_src, resolution_hz, mem_block_symbols; struct { bool invert_in; } flags; } rmt_rx_channel_config_t;
typedef struct { rmt_symbol_word_t *received_symbols; size_t num_symbols; } rmt_rx_done_event_data_t;
typedef struct { bool (*on_recv_done)(rmt_channel_handle_t, const rmt_rx_done_event_data_t *, void *); } rmt_rx_event_callbacks_t;
typedef struct { uint32_t signal_range_min_ns, signal_range_max_ns; } rmt_receive_config_t;
esp_err_t rmt_new_rx_channel(const rmt_rx_channel_config_t *, rmt_channel_handle_t *);
esp_err_t rmt_rx_register_event_callbacks(rmt_channel_handle_t, const rmt_rx_event_callbacks_t *, void *);
esp_err_t rmt_receive(rmt_channel_handle_t, void *, size_t, const rmt_receive_config_t *);
'''
MOCKS["resource_mgr.h"] = '''#pragma once
#include "esp_err.h"
#define RES_IR 0
#define RES_LANE_MAIN 0
#define RES_OWNER_UI 0
#define RES_HANDLE_NONE 0
typedef int res_handle_t;
typedef struct { int id, lane, owner_kind; bool allow_preempt; } res_request_t;
esp_err_t resource_acquire(const res_request_t *, res_handle_t *);
esp_err_t resource_release(res_handle_t);
'''
MOCKS["led_control.h"] = '''#pragma once
static inline void led_signal_info(void) {}
static inline void led_signal_warning(void) {}
static inline void led_signal_error(void) {}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="ir-remote-") as temporary:
        tmp = Path(temporary)
        for name, content in MOCKS.items():
            path = tmp / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(NOTICE + content)
        c5 = ROOT / "firmware_c5/components/Service/ir_remote"
        p4 = ROOT / "firmware_p4/components/Service/ir"
        (tmp / "c5.c").write_text(NOTICE + '#include "ir_remote.c"\nvoid test_c5_tick(void) { poll_once(); }\n')
        command = [*shlex.split(os.environ.get("CC", "cc")), "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                   "-fsanitize=address,undefined", "-Wno-unused-variable", "-g", "-I", str(tmp), "-I", str(c5),
                   "-I", str(c5 / "include"), "-I", str(p4 / "include"),
                   "-I", str(ROOT / "firmware_p4/components/Service/spi_bridge/include")]
        sources = [tmp / "c5.c", p4 / "ir.c", p4 / "ir_lease.c",
                   *sorted(p4.glob("ir_protocol*.c")), *sorted(p4.glob("ir_ac*.c")),
                   Path(__file__).with_name("test_remote.c")]
        binary = tmp / "test"
        subprocess.run(command + [str(p) for p in sources] + ["-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == "__main__":
    main()
