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

file(GLOB_RECURSE UI_SRCS "ui/*.c")
file(GLOB_RECURSE BOOT_UI_SRCS "ui/screens/boot/*.c")
file(GLOB_RECURSE HOME_UI_SRCS "ui/screens/home/*.c")
file(GLOB_RECURSE MENU_UI_SRCS "ui/screens/menu/*.c")
file(GLOB_RECURSE WIFI_UI_SRCS "ui/screens/wifi/*.c")
list(APPEND WIFI_UI_SRCS "ui/screens/wifi/wifi_scan_menu_ui.c")
list(APPEND WIFI_UI_SRCS "ui/screens/wifi/wifi_attack_menu_ui.c")
list(APPEND WIFI_UI_SRCS "ui/screens/wifi/wifi_packets_menu_ui.c")
list(APPEND WIFI_UI_SRCS "ui/screens/wifi/wifi_sniffer_raw_ui.c")
list(APPEND WIFI_UI_SRCS "ui/screens/wifi/wifi_sniffer_attack_ui.c")
list(APPEND WIFI_UI_SRCS "ui/screens/wifi/wifi_sniffer_handshake_ui.c")
list(APPEND WIFI_UI_SRCS "ui/screens/wifi/wifi_deauth_attack_ui.c")
list(APPEND WIFI_UI_SRCS "ui/screens/wifi/wifi_beacon_spam_simple_ui.c")
list(APPEND WIFI_UI_SRCS "ui/screens/wifi/wifi_probe_flood_ui.c")
list(APPEND WIFI_UI_SRCS "ui/screens/wifi/wifi_auth_flood_ui.c")
list(APPEND WIFI_UI_SRCS "ui/screens/wifi/wifi_scan_ap_ui.c")
list(APPEND WIFI_UI_SRCS "ui/screens/wifi/wifi_scan_stations_ui.c")
list(APPEND WIFI_UI_SRCS "ui/screens/wifi/wifi_scan_target_ui.c")
list(APPEND WIFI_UI_SRCS "ui/screens/wifi/wifi_scan_probe_ui.c")
list(APPEND WIFI_UI_SRCS "ui/screens/wifi/wifi_scan_monitor_ui.c")
file(GLOB_RECURSE BLE_UI_SRCS "ui/screens/bluetooth/*.c")
file(GLOB_RECURSE SUBGHZ_UI_SRCS "ui/screens/SubGhz/*.c")
file(GLOB_RECURSE SUB_EXAMPLE_UI_SRCS "ui/screens/sub_example/*.c")
file(GLOB_RECURSE BADUSB_UI_SRCS "ui/screens/badusb/*.c")

#COMPONENTS
file(GLOB_RECURSE HEADER_UI_SRCS "ui/components/header/*.c")
file(GLOB_RECURSE FOOTER_UI_SRCS "ui/components/footer/*.c")
file(GLOB_RECURSE KEYBOARD_UI_SRCS "ui/components/keyboard/*.c")
file(GLOB_RECURSE MSGBOX_UI_SRCS "ui/components/message_box/*.c")

#SETINGS SCREENS
file(GLOB_RECURSE SETTINGS_UI_SRCS "ui/screens/settings/*.c")
file(GLOB_RECURSE DISPLAY_SETTINGS_UI_SRCS "ui/screens/display_settings/*.c")
file(GLOB_RECURSE SOUND_SETTINGS_UI_SRCS "ui/screens/sound_settings/*.c")
file(GLOB_RECURSE INTERFACE_SETTINGS_UI_SRCS "ui/screens/interface_settings/*.c")
file(GLOB_RECURSE BATTERY_SETTINGS_UI_SRCS "ui/screens/battery_settings/*.c")
file(GLOB_RECURSE CONNECTION_SETTINGS_UI_SRCS "ui/screens/connection_settings/*.c")
file(GLOB_RECURSE ABOUT_SETTINGS_UI_SRCS "ui/screens/about_settings/*.c")

file(GLOB_RECURSE CONNECT_WIFI_UI_SRCS "ui/screens/connect_wifi/*.c")
file(GLOB_RECURSE CONNECT_BLUETOOTH_UI_SRCS "ui/screens/connect_bluetooth/*.c")


#APPLICATION
file(GLOB_RECURSE SUBGHZ_APP_SRCS "SubGhz/*.c")
file(GLOB_RECURSE BLE_APP_SRCS "bluetooth/*.c")
file(GLOB_RECURSE BADUSB_APP_SRCS "bad_usb/*.c")
file(GLOB_RECURSE ESPNOW_CHAT_APP_SRCS "espnow_chat/*.c")
file(GLOB_RECURSE WIFI_APP_SRCS "wifi/*.c")

idf_component_register(SRCS 
  "play/play.c"
  ${UI_SRCS}
  ${WIFI_APP_SRCS}
  ${ESPNOW_CHAT_APP_SRCS}
  ${BADUSB_APP_SRCS}
  ${BLE_APP_SRCS}
  "ui/ui_manager.c"
  "ui/assets_manager.c"
  ${SUBGHZ_APP_SRCS}
  ${SUBGHZ_UI_SRCS}
  ${BLE_UI_SRCS}
  ${BOOT_UI_SRCS}
  ${HOME_UI_SRCS}
  ${MENU_UI_SRCS}
  ${WIFI_UI_SRCS}
  ${SUB_EXAMPLE_UI_SRCS}
  ${BADUSB_UI_SRCS}
  ${HEADER_UI_SRCS}
  ${FOOTER_UI_SRCS}
  ${KEYBOARD_UI_SRCS}
  ${SETTINGS_UI_SRCS}
  ${DISPLAY_SETTINGS_UI_SRCS}
  ${INTERFACE_SETTINGS_UI_SRCS}
  ${SOUND_SETTINGS_UI_SRCS}
  ${BATTERY_SETTINGS_UI_SRCS}
  ${CONNECTION_SETTINGS_UI_SRCS}
  ${CONNECT_BLUETOOTH_UI_SRCS}
  ${CONNECT_WIFI_UI_SRCS}
  ${ABOUT_SETTINGS_UI_SRCS}
  ${MSGBOX_UI_SRCS}

  INCLUDE_DIRS 
  "play/include"
  "wifi/include"   
  "espnow_chat/include"
  "ui/include"
  "ui/screens/SubGhz/include"
  "ui/screens/bluetooth/include"
  "ui/screens/settings/include"
  "ui/screens/display_settings/include"
  "ui/screens/sound_settings/include"
  "ui/screens/interface_settings/include"
  "ui/screens/battery_settings/include"
  "ui/screens/connection_settings/include"
  "ui/screens/connect_wifi/include"
  "ui/screens/connect_bluetooth/include"
  "ui/screens/about_settings/include"
  "ui/screens/badusb/include"
  "bad_usb/include"
  "bluetooth/include"
  "SubGhz/include"
  "SubGhz/protocols/include"
  "ui/include"
  "ui/screens/home/include"
  "ui/screens/boot/include"
  "ui/screens/menu/include"
  "ui/screens/wifi/include"
  "ui/screens/sub_example/include"
  "ui/components/header/include"
  "ui/components/footer/include"
  "ui/components/keyboard/include"
  "ui/components/message_box/include"
  "ui/components/menu_component/include"
  "ui/components/button/include"
  "ui/components/terminal/include"
  


  REQUIRES 
  driver
  Drivers
  Service
  esp_common
  esp_wifi
  esp_tinyusb
  bt
)
target_link_libraries(${COMPONENT_LIB} -Wl,-zmuldefs)
