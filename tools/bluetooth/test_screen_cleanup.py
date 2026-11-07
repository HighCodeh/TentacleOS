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

"""Execute screen cleanup with active timers and radio operations."""
from pathlib import Path
import subprocess
import tempfile
import sys
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/nfc_api"))
from test_emu_tx import extract
base = ROOT / "firmware_p4/components/Applications/ui/screens/bluetooth"
for file, function, timer, stop in (
    ("ble_sniffer_ui", "ui_ble_sniffer_stop", "s_feed_timer", "ble_sniffer_stop"),
    ("ble_tracker_ui", "ui_ble_tracker_stop", "s_poll_timer", "tracker_detector_stop"),
    ("ble_skimmer_ui", "ui_ble_skimmer_stop", "s_scan_timer", "skimmer_detector_stop"),
    ("ble_exposure_ui", "ui_ble_exposure_stop", "s_feed_timer", "exposure_notification_stop"),
    ("ble_track_device_ui", "ui_ble_track_device_stop", "s_timer", "ble_tracker_stop"),
    ("ble_spam_ui", "ui_ble_spam_stop", "s_run_spam_timer", "spam_stop"),
    ("ble_flood_ui", "ui_ble_flood_stop", "s_flood_timer", "flood_mode_stop"),
):
    code = extract((base / (file + ".c")).read_text(), "void " + function + "(void)")
    fixture = """
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#define ESP_OK 0
#define ESP_LOGW(...) ((void)0)
#define PHASE_TRACK 1
#define PHASE_FLOOD 1
static void *TIMER = (void *)1;
static int stopped, deleted, deinitialized;
static void lv_timer_delete(void *t) { assert(t); deleted++; }
static void STOP(void) { stopped++; }
static int bluetooth_service_deinit(void) { assert(stopped); deinitialized++; return 0; }
""".replace("TIMER", timer).replace("STOP", stop)
    if "s_phase" in code: fixture += "static int s_phase = 1;\n"
    if "s_is_run_active" in code: fixture += "static bool s_is_run_active = true;\n"
    if "ble_sniffer_set_observer" in code:
        fixture += "static void ble_sniffer_set_observer(void *p) { assert(p == NULL); }\n"
    fixture += code
    fixture += f"\nint main(void) {{ {function}(); assert(!{timer} && stopped == 1 && deleted == 1 && deinitialized == 1); {function}(); assert(deleted == 1); }}"
    with tempfile.TemporaryDirectory(prefix="ble-stop-") as tmp:
        path = Path(tmp)
        (path / "test.c").write_text(fixture)
        subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", str(path / "test.c"), "-o", str(path / "test")], check=True)
        subprocess.run([str(path / "test")], check=True)
print("BLE screen stop, timer release and repeated cleanup passed")

for file, function, state, mocks, check in (
    ("ble_keyboard_ui", "ui_ble_keyboard_stop",
     "static void *s_phase_timer=(void *)1, *s_cursor_timer=(void *)2; static bool s_connected=true;",
     "", "assert(!s_phase_timer && !s_cursor_timer && !s_connected && deleted == 2);"),
    ("ble_mouse_ui", "ui_ble_mouse_stop",
     "static void *s_pair_timer=(void *)1, *s_mouse_timer=(void *)2;",
     "", "assert(!s_pair_timer && !s_mouse_timer && deleted == 2);"),
    ("ble_beacon_ui", "ui_beacon_spam_stop",
     "static bool s_adv_starting=true, s_adv_active=true; static int waits;",
     "#define ADV_STOP_POLL_MS 10\n#define pdMS_TO_TICKS(x) (x)\n"
     "static void vTaskDelay(int ticks) { assert(ticks == 10); if (++waits == 2) s_adv_starting=false; }\n"
     "static void bluetooth_service_stop_advertising(void) { assert(!s_adv_starting); }\n",
     "assert(!s_adv_active && waits == 2);"),
):
    code = extract((base / (file + ".c")).read_text(), "void " + function + "(void)")
    fixture = """
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#define ESP_OK 0
#define ESP_LOGW(...) ((void)0)
static int released, deleted;
static inline void lv_timer_delete(void *t) { assert(t); deleted++; }
static inline int ble_hid_deinit(void) { released++; return 0; }
static inline int bluetooth_service_deinit(void) { released++; return 0; }
""" + state + "\n" + mocks + code
    fixture += f"\nint main(void) {{ {function}(); {check} assert(released == 1); {function}(); assert(released == 2); }}"
    with tempfile.TemporaryDirectory(prefix="ble-hid-stop-") as tmp:
        path = Path(tmp)
        (path / "test.c").write_text(fixture)
        subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", str(path / "test.c"), "-o", str(path / "test")], check=True)
        subprocess.run([str(path / "test")], check=True)
print("HID timers and advertiser startup/stop ordering passed")

source = (ROOT / "firmware_c5/components/Service/spi_bridge/bt_dispatcher.c").read_text()
case = source[source.index("    case SPI_ID_BT_HID_DEINIT:"):source.index("    case SPI_ID_BT_HID_IS_CONNECTED:")]
fixture = """
#include <assert.h>
#include <stdbool.h>
#define ESP_OK 0
#define SPI_STATUS_OK 0
#define SPI_STATUS_ERROR 1
#define SPI_ID_BT_HID_DEINIT 1
static bool initialized;
static int stopped, released, failure;
static bool bluetooth_service_is_initialized(void) { return initialized; }
static int ble_hid_deinit(void) { assert(initialized); stopped++; return failure; }
static int bt_release_service_for_gatt(void) { released++; initialized=false; return 0; }
static int dispatch(int id) { switch(id) {
""" + case + """
 default: return 1;
}}
int main(void) {
 assert(dispatch(1)==0 && stopped==0 && released==0);
 initialized=true; failure=1;
 assert(dispatch(1)==1 && released==0);
 failure=0;
 assert(dispatch(1)==0 && stopped==2 && released==1);
 assert(dispatch(1)==0 && stopped==2 && released==1);
}
"""
with tempfile.TemporaryDirectory(prefix="ble-hid-dispatch-") as tmp:
    path = Path(tmp)
    (path / "test.c").write_text(fixture)
    subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("C5 HID shutdown releases stack and tolerates repeated stop")
