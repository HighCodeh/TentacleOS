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

file(GLOB_RECURSE HOME_UI_SRCS "ui/screens/home/*.c")
file(GLOB_RECURSE MENU_UI_SRCS "ui/screens/menu/*.c")
file(GLOB_RECURSE WIFI_UI_SRCS "ui/screens/wifi/*.c")
file(GLOB_RECURSE SUBGHZ_UI_SRCS "ui/screens/SubGhz/*.c")
file(GLOB_RECURSE SUB_EXAMPLE_UI_SRCS "ui/screens/sub_example/*.c")
file(GLOB_RECURSE HEADER_UI_SRCS "ui/components/header/*.c")
file(GLOB_RECURSE FOOTER_UI_SRCS "ui/components/footer/*.c")

file(GLOB_RECURSE SUBGHZ_APP_SRCS "SubGhz/*.c")


idf_component_register(SRCS 
  "play/play.c"
  "wifi/wifi_deauther.c"
  "wifi/evil_twin.c"
  "bad_usb/bad_usb.c"                           
  "bluetooth/apple_spam.c"
  "bluetooth/bluetooth_scanner.c"
  "ui/ui_manager.c"
  ${SUBGHZ_APP_SRCS}
  ${SUBGHZ_UI_SRCS}
  ${HOME_UI_SRCS}
  ${MENU_UI_SRCS}
  ${WIFI_UI_SRCS}
  ${SUB_EXAMPLE_UI_SRCS}
  ${HEADER_UI_SRCS}
  ${FOOTER_UI_SRCS}

  INCLUDE_DIRS 
  "play/include"
  "wifi/include"   
  "ui/screens/SubGhz/include"
  "bad_usb/include"
  "bluetooth/include"
  "SubGhz/include"
  "SubGhz/protocols/include"
  "ui/include"
  "ui/screens/home/include"
  "ui/screens/menu/include"
  "ui/screens/wifi/include"
  "ui/screens/sub_example/include"
  "ui/components/header/include"
  "ui/components/footer/include"
  


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
