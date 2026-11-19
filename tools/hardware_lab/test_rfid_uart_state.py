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

"""Execute the real firmware event loop against virtual time and peripheral failures."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
DRIVER = ROOT / "firmware_lab_rfid/components/Drivers/rfid_lab_io"
SERVICE = ROOT / "firmware_lab_rfid/components/Service/rfid_lab"

STUB = r"""
#ifndef SDK_STUB_H
#define SDK_STUB_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define UART_NUM_0 0
#define UART_DATA_8_BITS 8
#define UART_PARITY_DISABLE 0
#define UART_STOP_BITS_1 1
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_DEFAULT 0
#define UART_PIN_NO_CHANGE -1
#define pdMS_TO_TICKS(x) (x)
#define ESP_ERROR_CHECK(x) do { if ((x) != 0) abort(); } while (0)
#define ESP_LOGE(tag, ...) ((void)(tag))
#define REG_SET_BIT(a,b) ((void)0)
#define LP_SYSTEM_REG_SYS_CTRL_REG 0
#define LP_SYSTEM_REG_FORCE_DOWNLOAD_BOOT 0
typedef struct { int baud_rate,data_bits,parity,stop_bits,flow_ctrl,source_clk; } uart_config_t;
typedef struct { char project_name[32],version[32],idf_ver[32]; uint8_t app_elf_sha256[32]; } esp_app_desc_t;
const esp_app_desc_t *esp_app_get_description(void);
const char *esp_err_to_name(int);
int64_t esp_timer_get_time(void);
void esp_restart(void);
void vTaskDelay(int);
int uart_write_bytes(int,const void*,size_t);
int uart_read_bytes(int,void*,size_t,int);
int uart_driver_install(int,int,int,int,void*,int);
int uart_param_config(int,const uart_config_t*);
int uart_set_pin(int,int,int,int,int);
#endif
"""
WRAPPER = r"""
#include "rfid_lab.c"
static int64_t clock_us;
static int active_pin;
static uint32_t drive_hz;
static uint32_t external_hz;
static uint32_t cl_counter;
static uint32_t overflow;
static int start_error;
static int off_error;
static unsigned starts;
static char output[131072];
static size_t output_length;
static esp_app_desc_t descriptor;
const esp_app_desc_t *esp_app_get_description(void) { return &descriptor; }
const char *esp_err_to_name(int e) { return e ? "ERROR" : "ESP_OK"; }
int64_t esp_timer_get_time(void) { return clock_us; }
void esp_restart(void) { abort(); }
void vTaskDelay(int ms) { clock_us += ms * 1000; }
int uart_write_bytes(int u,const void *data,size_t n) {
 (void)u; if (output_length + n < sizeof(output)) { memcpy(output + output_length,data,n); output_length += n; output[output_length]=0; } return (int)n;
}
int uart_read_bytes(int u,void*b,size_t n,int t) { (void)u;(void)b;(void)n;(void)t; return 0; }
int uart_driver_install(int a,int b,int c,int d,void*e,int f) { (void)a;(void)b;(void)c;(void)d;(void)e;(void)f; return 0; }
int uart_param_config(int a,const uart_config_t*b) { (void)a;(void)b; return 0; }
int uart_set_pin(int a,int b,int c,int d,int e) { (void)a;(void)b;(void)c;(void)d;(void)e; return 0; }
esp_err_t rfid_lab_io_init(void) { return 0; }
esp_err_t rfid_lab_io_off(void) { if (off_error) return -1; active_pin=0; drive_hz=0; return 0; }
esp_err_t rfid_lab_io_pwm(uint32_t pin,uint32_t hz,uint32_t duty,uint32_t*out) {
 (void)duty; active_pin=pin; drive_hz=hz; *out=hz; starts++; return start_error;
}
esp_err_t rfid_lab_io_static(uint32_t pin,uint32_t level) { active_pin=level ? (int)pin : 0; starts++; return start_error; }
esp_err_t rfid_lab_io_emulate(uint32_t hz,bool invert) { (void)hz;(void)invert; active_pin=11; starts++; return start_error; }
esp_err_t rfid_lab_io_capture(bool enable) { (void)enable; return 0; }
esp_err_t rfid_lab_io_feedback(rfid_lab_feedback_t*out) {
 *out=(rfid_lab_feedback_t){.time_us=clock_us,.cl_rises=cl_counter,.dropped=overflow}; return 0;
}
size_t rfid_lab_io_pop(rfid_lab_edge_t*out,size_t capacity) { (void)out;(void)capacity; return 0; }
void fixture_reset(void) {
 clock_us=1; active_pin=0; drive_hz=external_hz=cl_counter=overflow=0;
 start_error=off_error=0; starts=0; output_length=0; output[0]=0;
 s_phase=RFID_LAB_IDLE; s_uses_capture=false;
}
void fixture_command(const char *text) { char b[128]; snprintf(b,sizeof(b),"%s",text); command(b); }
void fixture_tick(uint32_t ms) {
 cl_counter += (uint64_t)(external_hz + (active_pin == 2 ? drive_hz : 0)) * ms / 1000;
 clock_us += (int64_t)ms*1000; poll();
}
void fixture_external(uint32_t hz) { external_hz=hz; }
void fixture_fault(int start,int stop) { start_error=start; off_error=stop; }
void fixture_overflow(void) { overflow=1; }
int fixture_pin(void) { return active_pin; }
int fixture_phase(void) { return s_phase; }
unsigned fixture_starts(void) { return starts; }
const char *fixture_output(void) { return output; }
"""

class StateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        directory = Path(cls.temp.name)
        (directory / "sdk_stub.h").write_text(STUB)
        for name in ("esp_err.h", "driver/uart.h", "esp_app_desc.h", "esp_log.h", "esp_system.h",
                     "esp_timer.h", "freertos/FreeRTOS.h", "freertos/task.h", "soc/lp_system_reg.h", "soc/soc.h"):
            path = directory / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "sdk_stub.h"\n')
        (directory / "wrapper.c").write_text(WRAPPER)
        library = directory / "state.dylib"
        subprocess.run(["cc", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
                        "-I"+str(directory), "-I"+str(SERVICE), "-I"+str(SERVICE/"include"),
                        "-I"+str(DRIVER/"include"), "-I"+str(ROOT/"firmware_p4/components/Drivers/pins/include"),
                        str(directory/"wrapper.c"), str(SERVICE/"rfid_lab_command.c"),
                        str(DRIVER/"rfid_lab_protocol.c"), "-o", str(library)], check=True)
        cls.lib = C.CDLL(str(library))
        cls.lib.fixture_command.argtypes = [C.c_char_p]
        cls.lib.fixture_output.restype = C.c_char_p

    @classmethod
    def tearDownClass(cls): cls.temp.cleanup()
    def setUp(self): self.lib.fixture_reset()
    def command(self, text): self.lib.fixture_command(text.encode())
    def test_read_only_and_invalid_never_start_outputs(self):
        for text in ("help", "pins", "status", "info", "gpio 15 1", "field 999", "emulate 0"):
            self.command(text)
        self.assertEqual(self.lib.fixture_starts(), 0)
        self.assertEqual(self.lib.fixture_pin(), 0)
    def test_carrier_deadline_and_busy_interlock(self):
        self.command("field 125 50 100")
        self.assertEqual(self.lib.fixture_pin(), 0)
        self.lib.fixture_tick(50)
        self.assertEqual(self.lib.fixture_pin(), 2)
        self.command("pulse")
        self.assertEqual(self.lib.fixture_starts(), 1)
        self.lib.fixture_tick(99)
        self.assertEqual(self.lib.fixture_pin(), 2)
        self.lib.fixture_tick(1)
        self.assertEqual(self.lib.fixture_pin(), 0)
        self.assertEqual(self.lib.fixture_phase(), 0)
    def test_off_cancels_precheck_and_active(self):
        for text in ("field", "pulse", "gpio 11 1 1000", "sweep"):
            self.command(text)
            self.command("off")
            self.lib.fixture_tick(5000)
            self.assertEqual(self.lib.fixture_pin(), 0)
            self.assertEqual(self.lib.fixture_phase(), 0)
    def test_external_field_blocks_reader_but_allows_emulation(self):
        self.lib.fixture_external(125000)
        self.command("field")
        self.lib.fixture_tick(50)
        self.assertEqual(self.lib.fixture_starts(), 0)
        self.command("emulate 1000")
        self.lib.fixture_tick(50)
        self.assertEqual(self.lib.fixture_pin(), 11)
        self.lib.fixture_tick(1000)
        self.assertEqual(self.lib.fixture_pin(), 0)
    def test_emulation_without_external_field_refused(self):
        self.command("emulate")
        self.lib.fixture_tick(50)
        self.assertEqual(self.lib.fixture_starts(), 0)
    def test_sweep_all_nineteen_steps_off_between(self):
        self.command("sweep")
        for step in range(19):
            self.lib.fixture_tick(50)
            self.assertEqual(self.lib.fixture_pin(), 2)
            self.lib.fixture_tick(5000)
            self.assertEqual(self.lib.fixture_pin(), 0)
            if step < 18: self.lib.fixture_tick(250)
        self.assertEqual(self.lib.fixture_starts(), 19)
        self.assertEqual(self.lib.fixture_phase(), 0)
    def test_partial_start_failure_turns_outputs_off(self):
        self.lib.fixture_fault(-1, 0)
        self.command("pulse")
        self.assertEqual(self.lib.fixture_pin(), 0)
    def test_off_failure_blocks_next_operation(self):
        self.command("pulse")
        self.lib.fixture_fault(0, -1)
        self.command("off")
        self.assertEqual(self.lib.fixture_phase(), 4)
        self.command("field")
        self.assertEqual(self.lib.fixture_starts(), 1)
    def test_capture_overflow_ends_field_and_rejects_capture(self):
        self.command("read")
        self.lib.fixture_tick(50)
        self.lib.fixture_overflow()
        self.lib.fixture_tick(500)
        self.assertEqual(self.lib.fixture_pin(), 0)
        self.assertIn(b"INVALID TEST", self.lib.fixture_output())

if __name__ == "__main__": unittest.main()
