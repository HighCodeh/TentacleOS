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

file(GLOB_RECURSE SD_CARD_SERVICE_SRCS "sd_card/*.c")
file(GLOB_RECURSE CONSOLE_SERVICE_SRCS "console/*.c")
file(GLOB_RECURSE CONSOLE_COMMANDS_SRCS "console/commands/*.c")
file(GLOB_RECURSE C5_FLASHER_SRCS "c5_flasher/*.c")
file(GLOB_RECURSE C5_LEGACY_SRCS "c5_legacy/*.c")
file(GLOB_RECURSE LVGL_SRCS "lvgl/*.c")
file(GLOB_RECURSE SPI_BRIDGE_SRCS "spi_bridge/*.c")
file(GLOB_RECURSE BRIDGE_MANAGER_SRCS "bridge_manager/*.c")
file(GLOB_RECURSE BT_SERVICE_SRCS "bluetooth/*.c")
file(GLOB_RECURSE HOST_LINK_SRCS "host_link/*.c")
file(GLOB_RECURSE STORAGE_API_SRCS "storage_api/*.c")
file(GLOB_RECURSE STORAGE_VFS_SRCS "storage_vfs/*.c")
file(GLOB_RECURSE STORAGE_ASSETS_SRCS "storage_assets/*.c")

idf_component_register(SRCS
  "font/font.c"
  "icons/icons.c"
  "wifi/wifi_service.c"

  ${STORAGE_API_SRCS}
  ${STORAGE_VFS_SRCS}
  ${STORAGE_ASSETS_SRCS}

  "ir/ir.c"
  "ir/ir_file.c"
  "ir/ir_ac.c"
  "ir/ir_ac_coolix.c"
  "ir/ir_ac_gree.c"
  "ir/ir_ac_lg.c"
  "ir/ir_ac_midea.c"
  "ir/ir_ac_toshiba.c"
  "ir/ir_ac_haier.c"
  "ir/ir_protocol.c"
  "ir/ir_protocol_nec.c"
  "ir/ir_protocol_nec42.c"
  "ir/ir_protocol_samsung.c"
  "ir/ir_protocol_lg.c"
  "ir/ir_protocol_jvc.c"
  "ir/ir_protocol_denon.c"
  "ir/ir_protocol_panasonic.c"
  "ir/ir_protocol_rca.c"
  "ir/ir_protocol_pioneer.c"
  "ir/ir_protocol_rc6.c"
  "ir/ir_protocol_rc5.c"
  "ir/ir_protocol_sony.c"
  ${LVGL_SRCS}
  ${SD_CARD_SERVICE_SRCS}
  ${CONSOLE_SERVICE_SRCS}
  ${CONSOLE_COMMANDS_SRCS}
  ${C5_FLASHER_SRCS}
  ${C5_LEGACY_SRCS}
  ${SPI_BRIDGE_SRCS}
  ${BRIDGE_MANAGER_SRCS}
  ${BT_SERVICE_SRCS}
  ${HOST_LINK_SRCS}

  "ota/ota_service.c"

  INCLUDE_DIRS
  "font/include"
  "icons/include"
  "wifi/include"
  "ir/include"
  "storage_api/include"
  "storage_vfs/include"
  "storage_assets/include"
  "lvgl/include"
  "sd_card/include"
  "console/include"
  "spi_bridge/include"
  "host_link/include"
  "c5_flasher/include"
  "c5_legacy/include"
  "bridge_manager/include"
  "bluetooth/include"
  "ota/include"




  REQUIRES 
    sdmmc
    driver  
    vfs
    fatfs
    esp_timer
    esp_common
    Drivers
    Applications
    esp_http_server
    nvs_flash
    lvgl
    esp_lvgl_port
    esp_lcd
    littlefs
    cjson
    console
    argtable3
    app_update
    esp-serial-flasher
    esp_tinyusb
    mbedtls
    esp_hw_support
)

# C5 firmware is NOT embedded in the P4 binary anymore. c5_flasher.c streams the
# C5 app image from the micro-SD at /sdcard/c5/TentacleOS_C5.bin, which frees the
# whole C5 image (~1MB) from the P4 app slot. Both guards stay 0 on purpose; the
# ROM-recovery path (c5_rom_flasher.c) compiles to its graceful stub.
target_compile_definitions(${COMPONENT_LIB} PRIVATE C5_FIRMWARE_EMBEDDED=0)
target_compile_definitions(${COMPONENT_LIB} PRIVATE C5_ROM_IMAGES_EMBEDDED=0)
