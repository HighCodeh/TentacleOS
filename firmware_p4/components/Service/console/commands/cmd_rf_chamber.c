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
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "esp_console.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "rf_chamber.h"
#include "rf_passive.h"
#include "esp_system.h"
#define ARG_COUNT  12
#define MAX_NUMBER 1000000000L
static const char *TAG = "RF_BENCH";
static void report(void) {
  rf_chamber_status_t s;
  rf_chamber_snapshot(&s);
  ESP_LOGI(TAG,
           "STATUS "
           "{\"id\":%lu,\"phase\":%u,\"radio\":%u,\"mode\":%u,\"hz\":%lu,\"dbm\":%d,\"path\":%u,"
           "\"sf\":%u,\"bw\":%u,\"cr\":%u,\"duration_ms\":%lu,\"interval_ms\":%lu,\"elapsed_ms\":%"
           "lu,\"submitted\":%lu,\"done\":%lu,\"timeouts\":%lu,\"deferred\":%lu,\"rx_test\":%lu,"
           "\"rx_other\":%lu,\"rx_error\":%lu,\"seq_last\":%lu,\"seq_gaps\":%lu,\"seq_repeats\":%"
           "lu,\"samples\":%lu,\"rssi_sum_half\":%ld,\"rssi_min_half\":%d,\"rssi_max_half\":%d,"
           "\"snr\":%d,\"freqerr_raw\":%d,\"radio_status\":%u,\"pa\":%u,\"device_errors\":%u,"
           "\"error\":%d,\"cleanup_error\":%d,\"stopped\":%u}",
           (unsigned long)s.run_id,
           s.phase,
           s.config.radio,
           s.config.mode,
           (unsigned long)s.config.frequency_hz,
           s.config.power_dbm,
           s.config.path,
           s.config.sf,
           s.config.bandwidth_khz,
           s.config.cr,
           (unsigned long)s.config.duration_ms,
           (unsigned long)s.config.interval_ms,
           (unsigned long)s.elapsed_ms,
           (unsigned long)s.tx_submitted,
           (unsigned long)s.tx_done,
           (unsigned long)s.tx_timeout,
           (unsigned long)s.duty_deferred,
           (unsigned long)s.rx_test,
           (unsigned long)s.rx_other,
           (unsigned long)s.rx_error,
           (unsigned long)s.sequence_last,
           (unsigned long)s.sequence_gaps,
           (unsigned long)s.sequence_repeats,
           (unsigned long)s.samples,
           (long)s.rssi_sum_half_dbm,
           s.rssi_min_half_dbm,
           s.rssi_max_half_dbm,
           s.last_snr_db,
           s.frequency_error_raw,
           s.radio_status,
           s.pa_code,
           s.device_errors,
           s.error,
           s.cleanup_error,
           s.stopped_by_user);
}
static int cmd_rfbench(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "info") == 0) {
    const esp_app_desc_t *app = esp_app_get_description();
    static const char HEX[] = "0123456789abcdef";
    char sha[65];
    for (size_t i = 0; i < sizeof(app->app_elf_sha256); ++i) {
      sha[i * 2] = HEX[app->app_elf_sha256[i] >> 4];
      sha[i * 2 + 1] = HEX[app->app_elf_sha256[i] & 15];
    }
    sha[64] = '\0';
    ESP_LOGI(TAG, "INFO schema=1 version=%s elf_sha256=%s idf=%s", app->version, sha, app->idf_ver);
    return 0;
  }
  if (argc == 2 && strcmp(argv[1], "status") == 0) {
    report();
    return 0;
  }
  if (argc == 2 && strcmp(argv[1], "stop") == 0) {
    rf_chamber_stop();
    ESP_LOGI(TAG, "STOP requested; poll status until phase=0, cleanup_error=0");
    return 0;
  }
  if (argc != ARG_COUNT || strcmp(argv[11], "ARMED") != 0) {
    ESP_LOGI(TAG, "rfbench cc|sx cw|tx|rx HZ DBM PATH SF BW_KHZ CR MS INTERVAL_MS ARMED");
    ESP_LOGI(TAG,
             "rfbench status | stop; CW/TX are laboratory-only. CC: SF=7 BW=125 CR=1 placeholders; "
             "packet PHY is fixed 2-FSK.");
    return 1;
  }
  rf_chamber_config_t c = {0};
  if (strcmp(argv[1], "cc") == 0)
    c.radio = RF_CHAMBER_CC;
  else if (strcmp(argv[1], "sx") == 0)
    c.radio = RF_CHAMBER_SX;
  else
    return 1;
  if (strcmp(argv[2], "cw") == 0)
    c.mode = RF_CHAMBER_CW;
  else if (strcmp(argv[2], "tx") == 0)
    c.mode = RF_CHAMBER_TX;
  else if (strcmp(argv[2], "rx") == 0)
    c.mode = RF_CHAMBER_RX;
  else
    return 1;
  long values[8];
  for (int i = 0; i < 8; ++i) {
    char *end = NULL;
    errno = 0;
    values[i] = strtol(argv[i + 3], &end, 10);
    if (errno != 0 || end == argv[i + 3] || *end != '\0' || values[i] < -30 ||
        values[i] > MAX_NUMBER)
      return 1;
  }
  /* Check before narrowing; 263 must not become a valid SF7. */
  if (values[0] < 0 || values[1] < -30 || values[1] > 22 || values[2] < 0 || values[2] > 4 ||
      values[3] < 7 || values[3] > 12 || values[4] < 125 || values[4] > 500 || values[5] < 1 ||
      values[5] > 4 || values[6] < 0 || values[7] < 0)
    return 1;
  c.frequency_hz = values[0];
  c.power_dbm = values[1];
  c.path = values[2];
  c.sf = values[3];
  c.bandwidth_khz = values[4];
  c.cr = values[5];
  c.duration_ms = values[6];
  c.interval_ms = values[7];
  esp_err_t error = rf_chamber_start(&c);
  ESP_LOGI(TAG, "REQUEST %s", esp_err_to_name(error));
  return error == ESP_OK ? 0 : 1;
}
static int cmd_rfpassive(int argc, char **argv) {
  rf_passive_status_t status = {0};
  rf_passive_snapshot(&status);
  if (argc == 2 && strcmp(argv[1], "status") == 0) {
    ESP_LOGI(TAG,
             "PASSIVE boot=%u path=%u error=%d c5=%d cc=%d sx=%d nfc=%d lf=%d",
             rf_passive_is_boot(),
             status.path,
             status.error,
             status.hardware.c5,
             status.hardware.cc,
             status.hardware.sx,
             status.hardware.nfc,
             status.hardware.lf);
    return 0;
  }
  if (argc == 3 && strcmp(argv[1], "path") == 0 && strlen(argv[2]) == 1 && argv[2][0] >= '1' &&
      argv[2][0] <= '4')
    return rf_passive_select(argv[2][0] - '0') == ESP_OK ? 0 : 1;
  if (argc == 3 && strcmp(argv[2], "RESTART") == 0 &&
      (strcmp(argv[1], "on") == 0 || strcmp(argv[1], "off") == 0)) {
    rf_chamber_status_t bench;
    rf_chamber_snapshot(&bench);
    if (bench.phase != RF_CHAMBER_IDLE)
      return 1;
    esp_err_t error = rf_passive_save_boot(strcmp(argv[1], "on") == 0, status.path);
    if (error == ESP_OK)
      esp_restart();
    ESP_LOGE(TAG, "Passive boot save failed: %s", esp_err_to_name(error));
    return 1;
  }
  ESP_LOGI(TAG, "rfpassive status | path 1..4 | on|off RESTART");
  return 1;
}
void register_rf_chamber_commands(void) {
  const esp_console_cmd_t passive = {.command = "rfpassive",
                                     .help = "Passive switch and persistent lab boot",
                                     .func = cmd_rfpassive};
  ESP_ERROR_CHECK(esp_console_cmd_register(&passive));
  const esp_console_cmd_t command = {
      .command = "rfbench", .help = "Bounded RF chamber tests", .hint = NULL, .func = cmd_rfbench};
  esp_err_t error = esp_console_cmd_register(&command);
  if (error != ESP_OK)
    ESP_LOGE(TAG, "register failed: %s", esp_err_to_name(error));
}
