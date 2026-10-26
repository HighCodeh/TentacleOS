# Copyright (c) 2025 HIGH CODE LLC
# SPDX-License-Identifier: GPL-3.0-or-later
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

# The UI is a mirror of firmware_p4_prototype's screens (stubbed). One recursive
# glob picks up every screen, component and support file under ui/ (plus
# ui_manager.c and assets_manager.c at the ui/ root) — new screens compile
# automatically. INCLUDE_DIRS is the only hand-maintained part; add a screen's
# include/ dir there when a new screen area is introduced.
# Globs every ui/**/*.c (e.g. screens/files/usb_storage_ui.c). NOTE: adding a new
# .c under ui/ needs a CMake reconfigure to re-run this glob.
file(GLOB_RECURSE UI_SRCS "ui/*.c")

list(REMOVE_ITEM UI_SRCS
  "${CMAKE_CURRENT_SOURCE_DIR}/ui/screens/games/flappy_ui.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/ui/screens/games/octopet_ui.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/ui/screens/games/motion_ui.c"
)

# Application (non-UI) sources — the real drivers/services stay in the build.
file(GLOB_RECURSE SUBGHZ_APP_SRCS "SubGhz/*.c")
file(GLOB_RECURSE BLE_APP_SRCS "bluetooth/*.c")
file(GLOB_RECURSE BADUSB_APP_SRCS "bad_usb/*.c")
file(GLOB_RECURSE WIFI_APP_SRCS "wifi/*.c")
file(GLOB_RECURSE NFC_APP_SRCS "nfc/*.c")
file(GLOB_RECURSE RFID_APP_SRCS "rfid/*.c")
file(GLOB_RECURSE RNODE_APP_SRCS "LoRa/rnode/*.c")
file(GLOB MESHCORE_APP_SRCS "LoRa/meshcore/*.c")
file(GLOB MESHTASTIC_APP_SRCS "LoRa/meshtastic/*.c")
file(GLOB LORA_SESSION_SRCS "LoRa/session/*.c")

idf_component_register(SRCS
  "scripts/script_runner.c"
  ${UI_SRCS}
  ${WIFI_APP_SRCS}
  ${NFC_APP_SRCS}
  ${RFID_APP_SRCS}
  ${RNODE_APP_SRCS}
  ${MESHCORE_APP_SRCS}
  ${MESHTASTIC_APP_SRCS}
  ${LORA_SESSION_SRCS}
  ${BADUSB_APP_SRCS}
  ${BLE_APP_SRCS}
  ${SUBGHZ_APP_SRCS}

  INCLUDE_DIRS
  # --- application (non-ui) ---
  "scripts/include"
  "wifi/include"
  "nfc/include"
  "nfc/protocols/common/include"
  "nfc/protocols/common"
  "nfc/protocols/emv/include"
  "nfc/protocols/felica/include"
  "nfc/protocols/iso14443a/include"
  "nfc/protocols/iso14443b/include"
  "nfc/protocols/iso15693/include"
  "nfc/protocols/llcp/include"
  "nfc/protocols/mifare/include"
  "nfc/protocols/t1t/include"
  "nfc/protocols/t2t/include"
  "bad_usb/include"
  "bluetooth/include"
  "SubGhz/include"
  "SubGhz/protocols/include"
  "rfid/include"
  "rfid/protocols/include"
  "LoRa/rnode/include"
  "LoRa/meshcore/include"
  "LoRa/meshtastic/include"
  "LoRa/session/include"
  # --- ui core ---
  "ui/include"
  # --- ui screens (mirrors firmware_p4_prototype) ---
  "ui/screens/apps/include"
  "ui/screens/audio/include"
  "ui/screens/video/include"
  "ui/screens/images/include"
  "ui/screens/badusb/include"
  "ui/screens/bluetooth/include"
  "ui/screens/boot/include"
  "ui/screens/boot_report/include"
  "ui/screens/connect_bluetooth/include"
  "ui/screens/connect_wifi/include"
  "ui/screens/connection_settings/include"
  "ui/screens/dev/include"
  "ui/screens/files/include"
  "ui/screens/games/include"
  "ui/screens/gpio/include"
  "ui/screens/haptic/include"
  "ui/screens/home/include"
  "ui/screens/infrared/include"
  "ui/screens/lora/include"
  "ui/screens/menu/include"
  "ui/screens/nfc/include"
  "ui/screens/octobit/include"
  "ui/screens/power/include"
  "ui/screens/rfid/include"
  "ui/screens/safe_mode/include"
  "ui/screens/settings/include"
  "ui/screens/subghz/include"
  "ui/screens/theme/include"
  "ui/screens/wifi/include"
  # --- ui components ---
  "ui/components/button/include"
  "ui/components/capture_result/include"
  "ui/components/chart/include"
  "ui/components/chrome/include"
  "ui/components/dropdown/include"
  "ui/components/error/include"
  "ui/components/feedback/include"
  "ui/components/footer/include"
  "ui/components/hb_viz/include"
  "ui/components/header/include"
  "ui/components/intensity_bar/include"
  "ui/components/keyboard/include"
  "ui/components/menu_component/include"
  "ui/components/message_box/include"
  "ui/components/notify/include"
  "ui/components/octobit/include"
  "ui/components/power_policy/include"
  "ui/components/reboot/include"
  "ui/components/page_dots/include"
  "ui/components/sigwave/include"
  "ui/components/subghz_scope/include"
  "ui/components/spinner/include"
  "ui/components/terminal/include"
  "ui/components/text_viewer/include"
  "ui/components/toggle/include"
  "ui/components/tutorial/include"
  "ui/components/waves/include"

  REQUIRES
  firmware_crypto
  esp_driver_jpeg
  esp_driver_tsens
  esp_driver_rmt
  script_engine
  driver
  esp_driver_tsens
  esp_driver_jpeg
  Drivers
  Service
  gameboy
  doom
  esp_common
  esp_wifi
  esp_tinyusb
  mbedtls
  bt
  libsodium
  littlefs
  nvs_flash
  esp-dsp
  esp-libhelix-mp3
  resource_mgr
  muse_port
)
target_link_libraries(${COMPONENT_LIB} -Wl,-zmuldefs)
target_compile_definitions(${COMPONENT_LIB} PRIVATE MESH_HEADLESS=0)
# Ported screens/components use the LVGL idiom of casting a style setter to
# lv_anim_exec_xcb_t for animations; GCC14/IDF5 flags this under
# -Wcast-function-type. Downgrade it from error for this component.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=cast-function-type)

set_source_files_properties("ui/screens/apps/muse_ui.c" PROPERTIES COMPILE_OPTIONS "-Os;-flto=2;-ffat-lto-objects")

set_source_files_properties("ui/screens/games/games_menu_ui.c" PROPERTIES COMPILE_OPTIONS "-Os;-flto=2;-ffat-lto-objects")

set_source_files_properties("ui/screens/apps/grok_idle_ui.c" PROPERTIES COMPILE_OPTIONS "-Os;-flto=2;-ffat-lto-objects")

set_source_files_properties("ui/ui_manager.c" "ui/screens/settings/settings_ui.c" "ui/screens/apps/apps_ui.c" PROPERTIES COMPILE_OPTIONS "-Os")

file(GLOB HIGHBOY_SIZE_UI "${CMAKE_CURRENT_SOURCE_DIR}/ui/screens/settings/*.c")
set_source_files_properties(${HIGHBOY_SIZE_UI} PROPERTIES COMPILE_OPTIONS "-Os")

# Layout/control screens favor code size; radio, audio and emulator cores keep -O2.
file(GLOB HIGHBOY_SIZE_NFC_UI "${CMAKE_CURRENT_SOURCE_DIR}/ui/screens/nfc/*.c")
set_source_files_properties(
  "ui/ui_theme.c"
  "ui/screens/home/home_ui.c"
  "ui/screens/menu/menu_ui.c"
  "ui/screens/connect_wifi/connect_wifi_ui.c"
  ${HIGHBOY_SIZE_NFC_UI}
  PROPERTIES COMPILE_OPTIONS "-Os")

# Size optimization and LTO keep both script engines in the existing OTA slots.
target_compile_options(${COMPONENT_LIB} PRIVATE -Os -flto=2 -ffat-lto-objects)

set_source_files_properties("ui/screens/wifi/ui_wifi_esp_scan.c"
                           "wifi/wifi_scan_view.c" "wifi/wifi_quick_scan.c"
                           PROPERTIES COMPILE_OPTIONS "-Oz")
