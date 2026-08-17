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

#include "console_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_console.h"

#include "ir.h"

// IR is brought up lazily (no boot-time init). The rx/send commands are one-shot:
// they init the RMT channel on demand and release it again when the operation
// finishes, so the channel is not held for the rest of the session. 'flash' is
// the exception - it stays on until 'flash off' by design.

#define IR_RX_DEFAULT_TIMEOUT_MS 5000

static ir_protocol_t proto_from_name(const char *name) {
  for (ir_protocol_t p = IR_PROTO_UNKNOWN + 1; p < IR_PROTO_COUNT; p++) {
    if (strcasecmp(name, ir_protocol_name(p)) == 0)
      return p;
  }
  return IR_PROTO_UNKNOWN;
}

static void print_protocols(void) {
  printf("  protocols:");
  for (ir_protocol_t p = IR_PROTO_UNKNOWN + 1; p < IR_PROTO_COUNT; p++)
    printf(" %s", ir_protocol_name(p));
  printf("\n");
}

static int cmd_ir(int argc, char **argv) {
  if (argc < 2) {
    printf("usage: ir <rx|send|flash>\n");
    printf("  rx [timeout_ms]              wait for one frame and decode it (default %d ms)\n",
           IR_RX_DEFAULT_TIMEOUT_MS);
    printf("  send <proto> <addr> <cmd> [repeat]  transmit a code (addr/cmd accept 0x hex)\n");
    printf("  flash <on|off>              drive the emitter at full power (torch) / off\n");
    print_protocols();
    return 1;
  }

  if (strcmp(argv[1], "rx") == 0) {
    uint32_t timeout_ms = IR_RX_DEFAULT_TIMEOUT_MS;
    if (argc >= 3)
      timeout_ms = (uint32_t)strtoul(argv[2], NULL, 0);

    esp_err_t r = ir_rx_init();
    if (r != ESP_OK) {
      printf("ir: rx init failed: %s\n", esp_err_to_name(r));
      return 1;
    }
    ir_rx_prime();
    printf("ir: waiting up to %lu ms for a frame...\n", (unsigned long)timeout_ms);

    ir_data_t data;
    r = ir_receive(&data, timeout_ms);
    if (r == ESP_OK) {
      printf("ir: %s addr=0x%08lX cmd=0x%08lX%s\n",
             ir_protocol_name(data.protocol),
             (unsigned long)data.address,
             (unsigned long)data.command,
             data.repeat ? " [repeat]" : "");
    } else if (r == ESP_ERR_TIMEOUT) {
      printf("ir: no frame (timeout)\n");
    } else if (r == ESP_ERR_NOT_FOUND) {
      printf("ir: frame received but not decoded (unknown protocol)\n");
    } else {
      printf("ir: receive error: %s\n", esp_err_to_name(r));
    }
    ir_rx_deinit(); // one-shot: release the RX channel now that the capture is done
    return 0;
  }

  if (strcmp(argv[1], "send") == 0) {
    if (argc < 5) {
      printf("usage: ir send <proto> <addr> <cmd> [repeat]\n");
      print_protocols();
      return 1;
    }
    ir_data_t data = {0};
    data.protocol = proto_from_name(argv[2]);
    if (data.protocol == IR_PROTO_UNKNOWN) {
      printf("ir: unknown protocol '%s'\n", argv[2]);
      print_protocols();
      return 1;
    }
    data.address = (uint32_t)strtoul(argv[3], NULL, 0);
    data.command = (uint32_t)strtoul(argv[4], NULL, 0);
    data.repeat = (argc >= 6 && strtoul(argv[5], NULL, 0) != 0);

    esp_err_t r = ir_tx_init();
    if (r != ESP_OK) {
      printf("ir: tx init failed: %s\n", esp_err_to_name(r));
      return 1;
    }
    r = ir_send(&data);
    ir_tx_deinit(); // one-shot: release the TX channel now that the send is done
    if (r != ESP_OK) {
      printf("ir: send failed: %s\n", esp_err_to_name(r));
      return 1;
    }
    printf("ir: sent %s addr=0x%08lX cmd=0x%08lX\n",
           ir_protocol_name(data.protocol),
           (unsigned long)data.address,
           (unsigned long)data.command);
    return 0;
  }

  if (strcmp(argv[1], "flash") == 0) {
    if (argc < 3 || (strcmp(argv[2], "on") != 0 && strcmp(argv[2], "off") != 0)) {
      printf("usage: ir flash <on|off>\n");
      return 1;
    }
    if (strcmp(argv[2], "on") == 0) {
      esp_err_t r = ir_flash_on();
      if (r != ESP_OK) {
        printf("ir: flash on failed: %s\n", esp_err_to_name(r));
        return 1;
      }
      printf("ir: flash ON - emitter at full power (infrared, check with a phone camera)\n");
    } else {
      esp_err_t r = ir_flash_off();
      if (r != ESP_OK) {
        printf("ir: flash off failed: %s\n", esp_err_to_name(r));
        return 1;
      }
      printf("ir: flash OFF\n");
    }
    return 0;
  }

  printf("ir: unknown subcommand '%s'\n", argv[1]);
  return 1;
}

void register_ir_commands(void) {
  const esp_console_cmd_t cmd_ir_def = {
      .command = "ir",
      .help = "Infrared receive/transmit: ir rx | send | flash",
      .hint = NULL,
      .func = &cmd_ir,
  };
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&cmd_ir_def));
}
