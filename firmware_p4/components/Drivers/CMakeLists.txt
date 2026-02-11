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


idf_component_register(SRCS 
  "bq25896/bq25896.c"
  "buzzer/buzzer.c"
  "cc1101/cc1101.c"
  "pn7150/pn7150.c" 
  "st7789/st7789.c"
  "led/led_control.c"
  "spi/spi.c"
  "i2c_init/i2c_init.c"
  "tusb_desc/tusb_desc.c"
  "buttons_gpio/buttons_gpio.c"

  INCLUDE_DIRS 
  "bq25896/include"
  "buzzer/include"
  "cc1101/include"
  "pn7150/include"
  "st7789/include"
  "led/include"
  "pins/include"
  "spi/include"
  "i2c_init/include"
  "tusb_desc/include"
  "buttons_gpio/include"

  REQUIRES 
  driver
  Service
  esp_lcd
  PRIV_REQUIRES
  esp_tinyusb
  "espressif__led_strip")
