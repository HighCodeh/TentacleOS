// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#include "c5_flasher.h"

#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "buttons_gpio.h"
#include "console_service.h"
#include "pin_def.h"

// The P4 console UART0 and the C5's UART0 share a trace on the V2 board: the
// host PC's USB-serial TX and the C5's RX sit on the same net, so a byte the
// host sends physically reaches both the P4 (console RX) and the C5 (RX) at
// once - host->C5 needs no forwarding. Only C5->host must be forwarded in
// software: the C5's TX lands on the P4's UART1 RX (GPIO_C5_UART_RX_PIN), and we
// copy it out of UART0 TX so the host sees it.
//
// While passthrough is active the P4 must NOT drive the shared line, so UART1
// here is RX-only (no TX pin routed).

#define HOST_UART      UART_NUM_0
#define C5_UART        UART_NUM_1
#define UART_BAUD      115200
#define UART_BUF_BYTES 4096

void c5_passthrough_run(void) {
  // 1. Tear down the console REPL so it stops consuming bytes from UART0.
  console_service_stop();
  vTaskDelay(pdMS_TO_TICKS(100));

  // 2. Silence ESP logging - esptool expects a clean SLIP stream on UART0.
  esp_log_level_set("*", ESP_LOG_NONE);

  // 3. UART0 (host PC) - keep the chip-default console pins. Reinstall the
  //    driver so this task owns the RX queue.
  uart_driver_delete(HOST_UART);
  uart_config_t host_cfg = {
      .baud_rate = UART_BAUD,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };
  uart_driver_install(HOST_UART, UART_BUF_BYTES, UART_BUF_BYTES, 0, NULL, 0);
  uart_param_config(HOST_UART, &host_cfg);
  uart_set_pin(
      HOST_UART, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

  // 4. UART1 (C5) - RX-only on GPIO_C5_UART_RX_PIN (= C5 TX). DO NOT route TX,
  //    the shared line must stay a passive input here (see header comment).
  uart_driver_delete(C5_UART);
  uart_config_t c5_cfg = host_cfg;
  uart_driver_install(C5_UART, UART_BUF_BYTES, 0, 0, NULL, 0);
  uart_param_config(C5_UART, &c5_cfg);
  uart_set_pin(
      C5_UART, UART_PIN_NO_CHANGE, GPIO_C5_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

  const char *banner = "\r\n\r\n"
                       "*** C5 esptool passthrough active.\r\n"
                       "*** Run on the host:\r\n"
                       "***   esptool.py --chip esp32c5 -p <this_port> -b 115200 write_flash 0x0 "
                       "<bin>\r\n"
                       "*** Press the BACK button on the device to reboot and exit.\r\n\r\n";
  uart_write_bytes(HOST_UART, banner, strlen(banner));

  // 5. Forward C5 -> host forever. Small reads keep latency low so esptool
  //    doesn't time out waiting for the C5's SLIP response.
  static uint8_t buf[512];
  uint32_t last_btn_poll = 0;
  while (true) {
    int n = uart_read_bytes(C5_UART, buf, sizeof(buf), pdMS_TO_TICKS(10));
    if (n > 0) {
      uart_write_bytes(HOST_UART, (const char *)buf, n);
    }
    // Poll BACK button about 10x/s. Pressed -> reboot to exit passthrough.
    uint32_t now = xTaskGetTickCount();
    if (pdTICKS_TO_MS(now - last_btn_poll) > 100) {
      last_btn_poll = now;
      if (back_button_is_down()) {
        const char *bye = "\r\n*** Passthrough exiting - rebooting...\r\n";
        uart_write_bytes(HOST_UART, bye, strlen(bye));
        uart_wait_tx_done(HOST_UART, pdMS_TO_TICKS(500));
        esp_restart();
      }
    }
  }
}
