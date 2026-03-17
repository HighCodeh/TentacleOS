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
  "led/led_control.c"
  "spi/spi.c"
  "spi_slave/spi_slave_driver.c"
  "i2c_init/i2c_init.c"
  "buttons_gpio/buttons_gpio.c"

  INCLUDE_DIRS 
  "bq25896/include"
  "buzzer/include"
  "led/include"
  "pins/include"
  "spi/include"
  "spi_slave/include"
  "i2c_init/include"
  "buttons_gpio/include"

  REQUIRES 
  driver
  Service
  esp_lcd
  PRIV_REQUIRES
  "espressif__led_strip")
