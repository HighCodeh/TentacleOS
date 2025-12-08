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

file(GLOB_RECURSE HOME_SRCS "ui/screens/home/*.c")


idf_component_register(SRCS 
  "home/home.c"
  "menu/menu.c"
  "UART/UART.c"
  "GPIO/GPIO.c"
  "sub_menu/sub_menu.c"
  "menu_generic/menu_generic.c"
  "play/play.c"
  "wifi/wifi.c"
  "infrared/infrared.c"
  "wifi/wifi_deauther.c"
  "wifi/evil_twin.c"
  "wifi/wifi_analyzer.c"
  "wifi/traffic_analyzer.c"
  "brightness_ui/brightness_ui.c"
  "infrared/infrared.c"
  "bad_usb/bad_usb.c"                           
  "bad_usb/bad_usb_menu.c"
  "battery_ui/battery_ui.c"
  "keyboard_ui/keyboard_ui.c"
  "sd_browser/sd_browser.c"
  "bluetooth/apple_spam.c"
  "bluetooth/bluetooth_scanner.c"
  "bluetooth/bluetooth_menu.c"
  "bluetooth/rssi_analyser.c"
  "ui/ui_manager.c"
  ${HOME_SRCS}

  INCLUDE_DIRS 
  "home/include"
  "menu/include"
  "UART/include"
  "GPIO/include"
  "keyboard_ui/include"
  "sub_menu/include"
  "menu_generic/include"
  "play/include"
  "wifi/include"   
  "brightness_ui/include"    
  "infrared/include"
  "bad_usb/include"
  "battery_ui/include"
  "sd_browser/include"
  "bluetooth/include"
  "ui/include"
  "ui/screens/home/include"


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
