#!/usr/bin/env python3
"""Reject the inherited RNode UART before it can claim P4 SDMMC pins."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
RNODE = ROOT / "firmware_p4/components/Applications/LoRa/rnode"


def main():
    with tempfile.TemporaryDirectory(prefix="tos-rnode-transport-") as temporary:
        directory = Path(temporary)
        (directory / "driver").mkdir()
        (directory / "sdkconfig.h").write_text('#define CONFIG_IDF_TARGET_ESP32P4 1\n')
        (directory / "esp_err.h").write_text(
            'typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_NOT_SUPPORTED 1\n')
        (directory / "esp_log.h").write_text(
            '#define ESP_LOGE(tag, ...) ((void)(tag))\n'
            '#define ESP_LOGI(tag, ...) ((void)(tag))\n')
        (directory / "driver/uart.h").write_text('''
#include <stddef.h>
#define UART_NUM_0 0
#define UART_DATA_8_BITS 8
#define UART_PARITY_DISABLE 0
#define UART_STOP_BITS_1 1
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_DEFAULT 0
#define UART_PIN_NO_CHANGE -1
typedef struct { int baud_rate, data_bits, parity, stop_bits, flow_ctrl, source_clk; } uart_config_t;
int uart_driver_install(int, int, int, int, void *, int);
int uart_param_config(int, const uart_config_t *);
int uart_set_pin(int, int, int, int, int);
int uart_write_bytes(int, const char *, size_t);
int uart_read_bytes(int, void *, size_t, int);
''')
        (directory / "test.c").write_text('''
#include <assert.h>
#include <stdlib.h>
#include "driver/uart.h"
#include "rnode_serial.h"
int uart_driver_install(int a,int b,int c,int d,void *e,int f) { abort(); }
int uart_param_config(int a,const uart_config_t *b) { abort(); }
int uart_set_pin(int a,int b,int c,int d,int e) { abort(); }
int uart_write_bytes(int a,const char *b,size_t c) { abort(); }
int uart_read_bytes(int a,void *b,size_t c,int d) { abort(); }
int main(void) {
  uint8_t buffer[1] = {0};
  assert(!rnode_serial_is_supported());
  assert(rnode_serial_init() == ESP_ERR_NOT_SUPPORTED);
  assert(rnode_serial_read(buffer, sizeof(buffer)) == -1);
  assert(rnode_serial_write(buffer, sizeof(buffer)) == -1);
  return 0;
}
''')
        executable = directory / "test"
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra",
                        "-Werror", "-Wno-unused-parameter", "-fsanitize=undefined",
                        "-I", str(directory), "-I", str(RNODE / "include"),
                        str(RNODE / "rnode_serial.c"), str(directory / "test.c"),
                        "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True, timeout=30)
        source = (RNODE / "rnode.c").read_text()
        init = source[source.index("esp_err_t rnode_init"):source.index("esp_err_t rnode_start")]
        assert init.index("rnode_serial_is_supported") < init.index("sx1262_init")
        print("PASS: P4 RNode rejects UART init/read/write before any hardware access")


if __name__ == "__main__":
    main()
