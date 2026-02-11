// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#ifndef PIN_DEF_H
#define PIN_DEF_H

// SPI
#define SPI_MOSI_PIN    11
#define SPI_SCLK_PIN    12
#define SPI_MISO_PIN    13

// extra gpios
#define GPIO_CS_PIN     3
#define GPIO_SDA_PIN    8  // GDO0
#define GPIO_SCL_PIN    9  // GDO2

// chip select
#define SD_CARD_CS_PIN  14
#define ST7789_CS_PIN   48

// buttons
#define BTN_LEFT     5
#define BTN_BACK     7
#define BTN_UP     15
#define BTN_DOWN   6
#define BTN_OK     4
#define BTN_RIGHT 16

// led
#define LED_GPIO     45
#define LED_COUNT    1

// lcd st7789
#define ST7789_PIN_CS   48
#define ST7789_PIN_DC   47
#define ST7789_PIN_RST  21
#define ST7789_PIN_BL   38

#endif // !PIN_DEF_H
