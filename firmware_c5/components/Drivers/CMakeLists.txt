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


idf_component_register(SRCS 
  "bq25896/bq25896.c"
  "led/led_control.c"
  "spi/spi.c"
  "spi_slave/spi_slave_driver.c"
  "i2c_init/i2c_init.c"
  "buttons_gpio/buttons_gpio.c"

  INCLUDE_DIRS
  "sys_prio/include"
  "bq25896/include"
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
