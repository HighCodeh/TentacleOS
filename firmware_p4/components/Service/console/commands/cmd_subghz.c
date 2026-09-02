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

#include "esp_console.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "cc1101.h"
#include "subghz_receiver.h"
#include "subghz_keeloq.h"
#include "subghz_protocol_registry.h"
#include "subghz_replay.h"
#include "subghz_spectrum.h"
#include "subghz_storage.h"

#define SUBGHZ_RX_DEFAULT_S    10
#define SUBGHZ_SPEC_DEFAULT_N  5
#define SUBGHZ_POLL_MS         50

static int subghz_rx(int argc, char **argv) {
  if (argc < 3) {
    printf("usage: subghz rx <freq_hz> [seconds] [raw]\n");
    return 1;
  }
  uint32_t freq = (uint32_t)strtoul(argv[2], NULL, 0);
  int seconds = (argc >= 4) ? (int)strtoul(argv[3], NULL, 0) : SUBGHZ_RX_DEFAULT_S;
  subghz_mode_t mode = SUBGHZ_MODE_SCAN;
  if (argc >= 5 && strcmp(argv[4], "raw") == 0)
    mode = SUBGHZ_MODE_RAW;

  if (subghz_receiver_start(mode, CC1101_PRESET_OOK_650KHZ, freq) != ESP_OK) {
    printf("subghz: receiver start failed\n");
    return 1;
  }
  printf("subghz: listening on %lu Hz for %d s...\n", (unsigned long)freq, seconds);

  subghz_rx_result_t res;
  uint32_t last_seq = 0;
  int ticks = (seconds * 1000) / SUBGHZ_POLL_MS;
  for (int i = 0; i < ticks; i++) {
    if (subghz_receiver_get_result(&res) && res.seq != 0 && res.seq != last_seq) {
      last_seq = res.seq;
      if (res.decoded)
        printf("  [%lu] %s serial=0x%lX btn=%u bits=%u raw=0x%lX\n",
               (unsigned long)res.seq,
               res.data.protocol_name ? res.data.protocol_name : "?",
               (unsigned long)res.data.serial,
               res.data.btn,
               res.data.bit_count,
               (unsigned long)res.data.raw_value);
      else
        printf("  [%lu] raw capture (saved as %s)\n", (unsigned long)res.seq, res.save_name);
    }
    vTaskDelay(pdMS_TO_TICKS(SUBGHZ_POLL_MS));
  }
  subghz_receiver_stop();
  printf("subghz: stopped\n");
  return 0;
}

static int subghz_spectrum(int argc, char **argv) {
  if (argc < 4) {
    printf("usage: subghz spectrum <center_hz> <span_hz> [lines]\n");
    return 1;
  }
  uint32_t center = (uint32_t)strtoul(argv[2], NULL, 0);
  uint32_t span = (uint32_t)strtoul(argv[3], NULL, 0);
  int lines = (argc >= 5) ? (int)strtoul(argv[4], NULL, 0) : SUBGHZ_SPEC_DEFAULT_N;

  subghz_spectrum_start(center, span);
  printf("subghz: spectrum @ %lu Hz span %lu Hz\n", (unsigned long)center, (unsigned long)span);

  subghz_spectrum_line_t line;
  int got = 0;
  for (int i = 0; i < 200 && got < lines; i++) {
    if (subghz_spectrum_get_line(&line)) {
      float peak = -200.0f;
      int peak_bin = 0;
      for (int b = 0; b < SPECTRUM_SAMPLES; b++) {
        if (line.dbm_values[b] > peak) {
          peak = line.dbm_values[b];
          peak_bin = b;
        }
      }
      uint32_t peak_freq = line.start_freq + (uint32_t)peak_bin * line.step_hz;
      printf("  peak %.0f dBm @ %lu Hz\n", peak, (unsigned long)peak_freq);
      got++;
    }
    vTaskDelay(pdMS_TO_TICKS(SUBGHZ_POLL_MS));
  }
  subghz_spectrum_stop();
  return 0;
}

static int subghz_list(void) {
  subghz_storage_init();
  subghz_storage_entry_t entries[32];
  int n = subghz_storage_list(entries, 32);
  if (n <= 0) {
    printf("subghz: no saved captures\n");
    return 0;
  }
  printf("subghz: %d capture(s):\n", n);
  for (int i = 0; i < n; i++)
    printf("  %-24s %-10s %lu Hz\n",
           entries[i].name,
           entries[i].protocol,
           (unsigned long)entries[i].frequency);
  return 0;
}

static int cmd_subghz(int argc, char **argv) {
  if (argc < 2) {
    printf("usage: subghz <rx|spectrum|list|replay|delete|selftest>\n");
    printf("  rx <freq_hz> [seconds] [raw]     receive/decode for a window\n");
    printf("  spectrum <center> <span> [lines] RSSI sweep, print peak per line\n");
    printf("  list                             list saved captures\n");
    printf("  replay <name>                    replay a saved capture\n");
    printf("  delete <name>                    delete a saved capture\n");
    printf("  selftest                         round-trip the CAME/RCSwitch encoders\n");
    return 1;
  }

  if (strcmp(argv[1], "rx") == 0)
    return subghz_rx(argc, argv);
  if (strcmp(argv[1], "spectrum") == 0)
    return subghz_spectrum(argc, argv);
  if (strcmp(argv[1], "list") == 0)
    return subghz_list();

  if (strcmp(argv[1], "selftest") == 0) {
    char report[192];
    bool ok = subghz_protocol_registry_selftest(report, sizeof(report));
    printf("%s", report);

    char kreport[288];
    subghz_keeloq_load_mfkeys();
    bool kok = subghz_keeloq_selftest(kreport, sizeof(kreport));
    printf("%s", kreport);

    bool all = ok && kok;
    printf("subghz: selftest %s\n", all ? "PASS" : "FAIL");
    return all ? 0 : 1;
  }

  if (strcmp(argv[1], "replay") == 0) {
    if (argc < 3) {
      printf("usage: subghz replay <name>\n");
      return 1;
    }
    subghz_storage_init();
    esp_err_t r = subghz_replay_file(argv[2]);
    printf("subghz: replay %s\n", r == ESP_OK ? "sent" : "failed");
    return r == ESP_OK ? 0 : 1;
  }

  if (strcmp(argv[1], "delete") == 0) {
    if (argc < 3) {
      printf("usage: subghz delete <name>\n");
      return 1;
    }
    subghz_storage_init();
    esp_err_t r = subghz_storage_delete(argv[2]);
    printf("subghz: delete %s\n", r == ESP_OK ? "ok" : "failed");
    return r == ESP_OK ? 0 : 1;
  }

  printf("subghz: unknown subcommand '%s'\n", argv[1]);
  return 1;
}

void register_subghz_commands(void) {
  const esp_console_cmd_t cmd_subghz_def = {
      .command = "subghz",
      .help = "Sub-GHz (CC1101): subghz rx | spectrum | list | replay | delete",
      .hint = NULL,
      .func = &cmd_subghz,
  };
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&cmd_subghz_def));
}
