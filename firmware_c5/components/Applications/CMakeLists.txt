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


#APPLICATION
file(GLOB_RECURSE BLE_APP_SRCS "bluetooth/*.c")
file(GLOB_RECURSE ESPNOW_CHAT_APP_SRCS "espnow_chat/*.c")
file(GLOB_RECURSE WIFI_APP_SRCS "wifi/*.c")

idf_component_register(SRCS 
  ${WIFI_APP_SRCS}
  ${ESPNOW_CHAT_APP_SRCS}
  ${BLE_APP_SRCS}

  INCLUDE_DIRS 
  "wifi/include"   
  "espnow_chat/include"
  "bluetooth/include"


  REQUIRES 
  driver
  Drivers
  Service
  esp_common
  esp_wifi
  bt
)
target_link_libraries(${COMPONENT_LIB} -Wl,-zmuldefs)
