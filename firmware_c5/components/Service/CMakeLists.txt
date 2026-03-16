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

file(GLOB_RECURSE CONSOLE_SERVICE_SRCS "console/*.c")
file(GLOB_RECURSE CONSOLE_COMMANDS_SRCS "console/commands/*.c")
file(GLOB_RECURSE SPI_BRIDGE_SRCS "spi_bridge/*.c")
file(GLOB_RECURSE SD_CARD_SRCS "sd_card/*.c")


idf_component_register(SRCS 
  "wifi/wifi_service.c"
  "wifi/mac_vendor.c"
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

  "esp_now/service_esp_now.c"
  ${SPI_BRIDGE_SRCS}
  ${CONSOLE_SERVICE_SRCS}
  ${CONSOLE_COMMANDS_SRCS}
  ${SD_CARD_SRCS}
  INCLUDE_DIRS 
  "wifi/include"
  "http_server/include"
  "dns_server/include"
  "bluetooth/include"
  "storage_api/include"
  "storage_vfs/include"
  "storage_assets/include"
  "esp_now/include"
  "spi_bridge/include"
  "console/include"
  "sd_card/include"




  REQUIRES 
    sdmmc
    driver  
    vfs
    fatfs
    esp_timer
    esp_common
    esp_netif
    esp_app_format
    Drivers
    Applications
    esp_http_server
    nvs_flash
    lvgl
    littlefs
    cjson
    console 
    argtable3 
)
