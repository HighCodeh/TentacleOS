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

file(GLOB_RECURSE ST25R3916_SRCS "st25r3916/*.c")
file(GLOB_RECURSE YS_RFID2_SRCS "ys_rfid2/*.c")
file(GLOB_RECURSE SX1262_SRCS "sx1262/*.c")


idf_component_register(SRCS
  "bq25896/bq25896.c"
  "bq25896/bq25896_ext.c"
  "bq25896/battery_service.c"
  "cc1101/cc1101.c"
  "st7789/st7789.c"
  "led/led_control.c"
  "spi/spi.c"
  "i2c_init/i2c_init.c"
  "tusb_desc/tusb_desc.c"
  "buttons_gpio/buttons_gpio.c"
  "input_manager/input_manager.c"
  "spi_bridge_phy/spi_bridge_phy.c"
  "audio_i2s/audio_i2s.c"
  "drv2605l/drv2605l.c"
  ${ST25R3916_SRCS}
  ${YS_RFID2_SRCS}
  ${SX1262_SRCS}

  INCLUDE_DIRS
  "sys_prio/include"
  "input_manager/include"
  "bq25896/include"
  "cc1101/include"
  "st7789/include"
  "led/include"
  "pins/include"
  "spi/include"
  "i2c_init/include"
  "tusb_desc/include"
  "buttons_gpio/include"
  "spi_bridge_phy/include"
  "audio_i2s/include"
  "drv2605l/include"
  "st25r3916/include"
  "st25r3916/hal/include"
  "ys_rfid2/include"
  "ys_rfid2/hal/include"
  "sx1262/include"

  REQUIRES
  driver
  esp_driver_i2s
  Service
  esp_lcd
  PRIV_REQUIRES
  esp_tinyusb
  soc
  "espressif__led_strip")
