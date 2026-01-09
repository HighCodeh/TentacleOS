# Copyright (c) 2025 HIGH CODE LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

file(GLOB_RECURSE SD_CARD_SERVICE_SRCS "sd_card/*.c")


idf_component_register(SRCS 
  "font/font.c"
  "icons/icons.c"
  "wifi/wifi_service.c"
  "http_server/http_server_service.c"
  "dns_server/dns_server.c"
  "bluetooth/bluetooth_service.c"

  "storage_api/storage_impl.c"
  "storage_api/storage_init.c"
  "storage_api/storage_read.c"
  "storage_api/storage_write.c"
  "storage_api/storage_mkdir.c"
  "storage_vfs/vfs_auto.c"
  "storage_vfs/vfs_core.c"
  "storage_vfs/vfs_littlefs.c"
  "storage_vfs/vfs_sdcard.c"
  "storage_assets/storage_assets.c"

  "ir/ir_encoder.c"
  "ir/ir_common.c"
  "ir/ir_tx.c"
  "ir/ir_rx.c"
  "ir/ir_storage.c"
  "ir/protocol_nec.c"
  "ir/protocol_rc6.c"
  "ir/protocol_rc5.c"
  "ir/protocol_samsung32.c"
  "ir/protocol_sony.c"
  "ir/ir_burst.c"
  "lvgl_port/lv_port_indev.c"
  "lvgl_port/lv_port_disp.c"
  "esp_now/service_esp_now.c"
  ${SD_CARD_SERVICE_SRCS}
  INCLUDE_DIRS 
  "font/include"
  "icons/include"
  "wifi/include"
  "http_server/include"
  "dns_server/include"
  "bluetooth/include"
  "ir/include"
  "storage_api/include"
  "storage_vfs/include"
  "storage_assets/include"
  "lvgl_port/include"
  "esp_now/include"
  "sd_card/include"

  REQUIRES 
    sdmmc
    driver  
    vfs
    fatfs
    esp_timer
    esp_wifi
    esp_common
    esp_netif
    Drivers 
    esp_http_server
    nvs_flash
    lvgl
    bt
    littlefs
    cjson
)
