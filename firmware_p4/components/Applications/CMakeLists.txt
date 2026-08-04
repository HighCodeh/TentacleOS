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

# The UI is a mirror of firmware_p4_prototype's screens (stubbed). One recursive
# glob picks up every screen, component and support file under ui/ (plus
# ui_manager.c and assets_manager.c at the ui/ root) — new screens compile
# automatically. INCLUDE_DIRS is the only hand-maintained part; add a screen's
# include/ dir there when a new screen area is introduced.
file(GLOB_RECURSE UI_SRCS "ui/*.c")

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

idf_component_register(SRCS
  ${UI_SRCS}
  ${WIFI_APP_SRCS}
  ${NFC_APP_SRCS}
  ${RFID_APP_SRCS}
  ${RNODE_APP_SRCS}
  ${MESHCORE_APP_SRCS}
  ${MESHTASTIC_APP_SRCS}
  ${BADUSB_APP_SRCS}
  ${BLE_APP_SRCS}
  ${SUBGHZ_APP_SRCS}

  INCLUDE_DIRS
  # --- application (non-ui) ---
  "wifi/include"
  "nfc/include"
  "nfc/protocols/common/include"
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
  # --- ui core ---
  "ui/include"
  # --- ui screens (mirrors firmware_p4_prototype) ---
  "ui/screens/audio/include"
  "ui/screens/badusb/include"
  "ui/screens/bluetooth/include"
  "ui/screens/boot/include"
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
  "ui/components/header/include"
  "ui/components/intensity_bar/include"
  "ui/components/keyboard/include"
  "ui/components/menu_component/include"
  "ui/components/message_box/include"
  "ui/components/notify/include"
  "ui/components/octobit/include"
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
  driver
  Drivers
  Service
  esp_common
  esp_wifi
  esp_tinyusb
  mbedtls
  bt
  libsodium
  mqtt
  joltwallet__littlefs
  nvs_flash
  espressif__esp-dsp
)
target_link_libraries(${COMPONENT_LIB} -Wl,-zmuldefs)
target_compile_definitions(${COMPONENT_LIB} PRIVATE MESH_HEADLESS=0)
# Ported screens/components use the LVGL idiom of casting a style setter to
# lv_anim_exec_xcb_t for animations; GCC14/IDF5 flags this under
# -Wcast-function-type. Downgrade it from error for this component.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=cast-function-type)
