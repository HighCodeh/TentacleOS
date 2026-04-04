// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#include "ble_tracker.h"
#include "bluetooth_service.h"
#include "esp_log.h"

static const char *TAG = "BLE_TRACKER";
static int latest_rssi = -120;

static void tracker_rssi_callback(int rssi) {
  latest_rssi = rssi;
}

esp_err_t ble_tracker_start(const uint8_t *addr) {
  latest_rssi = -120;
  return bluetooth_service_start_tracker(addr, tracker_rssi_callback);
}

void ble_tracker_stop(void) {
  bluetooth_service_stop_tracker();
}

int ble_tracker_get_rssi(void) {
  return latest_rssi;
}
