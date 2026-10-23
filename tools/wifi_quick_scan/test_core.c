// SPDX-License-Identifier: GPL-3.0-or-later
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

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "wifi_quick_scan_core.h"

static wifi_quick_scan_core_t core;
static const uint8_t esp_mac[6] = {0x24, 0x6F, 0x28, 1, 2, 3};

static void observe(const uint8_t *mac, uint32_t ms, int8_t rssi, uint16_t evidence) {
  wifi_quick_scan_core_observation_t observation = {
      .channel = 6, .rssi = rssi, .evidence = evidence, .elapsed_ms = ms,
  };
  memcpy(observation.mac, mac, 6);
  wifi_quick_scan_core_observe(&core, &observation);
}

static void test_parser(void) {
  uint8_t frame[80] = {0x40};
  memcpy(frame + 10, esp_mac, 6);
  wifi_quick_scan_core_observation_t observation;
  assert(wifi_quick_scan_core_parse(NULL, 24, &observation) == ESP_ERR_INVALID_ARG);
  assert(wifi_quick_scan_core_parse(frame, 24, NULL) == ESP_ERR_INVALID_ARG);
  for (size_t size = 0; size < 24; size++) {
    assert(wifi_quick_scan_core_parse(frame, size, &observation) != ESP_OK);
  }
  assert(wifi_quick_scan_core_parse(frame, 24, &observation) == ESP_OK);
  assert(memcmp(observation.mac, esp_mac, 6) == 0);
  assert(observation.evidence == (SPI_WIFI_QUICK_PROBE | SPI_WIFI_QUICK_ESPRESSIF_OUI));
  const uint8_t c5_ap_mac[6] = {0x10, 0xBD, 0xA3, 0xC0, 0x8C, 0xB9};
  memcpy(frame + 10, c5_ap_mac, sizeof(c5_ap_mac));
  assert(wifi_quick_scan_core_parse(frame, 24, &observation) == ESP_OK);
  assert(observation.evidence & SPI_WIFI_QUICK_ESPRESSIF_OUI);
  frame[10] |= 0x02;
  assert(wifi_quick_scan_core_parse(frame, 24, &observation) == ESP_OK);
  assert((observation.evidence & SPI_WIFI_QUICK_ESPRESSIF_OUI) == 0);
  memcpy(frame + 10, esp_mac, sizeof(esp_mac));
  frame[0] = 0x80;
  assert(wifi_quick_scan_core_parse(frame, 36, &observation) == ESP_OK);
  assert(observation.evidence & SPI_WIFI_QUICK_BEACON);
  frame[0] = 0x08;
  assert(wifi_quick_scan_core_parse(frame, 24, &observation) == ESP_OK);
  assert(observation.evidence & SPI_WIFI_QUICK_OTHER);
  // ACK/CTS have no transmitter address; never attribute addr1 to the sender.
  frame[0] = 0xD4;
  assert(wifi_quick_scan_core_parse(frame, 24, &observation) != ESP_OK);
  frame[0] = 0xB4;
  assert(wifi_quick_scan_core_parse(frame, 16, &observation) == ESP_OK);
  frame[0] = 0x40;
  frame[10] |= 0x02;
  assert(wifi_quick_scan_core_parse(frame, 24, &observation) == ESP_OK);
  assert(observation.evidence & SPI_WIFI_QUICK_LOCAL_MAC);
  assert(!(observation.evidence & SPI_WIFI_QUICK_ESPRESSIF_OUI));
  frame[10] |= 0x01;
  assert(wifi_quick_scan_core_parse(frame, 24, &observation) != ESP_OK);
  memset(frame + 10, 0, 6);
  assert(wifi_quick_scan_core_parse(frame, 24, &observation) != ESP_OK);
}

static void test_fingerprint(void) {
  uint8_t frame[39] = {0xD0};
  memcpy(frame + 10, esp_mac, 6);
  const uint8_t body[] = {127, 0x18, 0xFE, 0x34, 0, 0, 0, 0,
                          221, 5, 0x18, 0xFE, 0x34, 4, 1};
  memcpy(frame + 24, body, sizeof(body));
  wifi_quick_scan_core_observation_t observation;
  for (size_t size = 24; size < sizeof(frame); size++) {
    assert(wifi_quick_scan_core_parse(frame, size, &observation) == ESP_OK);
    assert(!(observation.evidence & SPI_WIFI_QUICK_ESPNOW));
  }
  assert(wifi_quick_scan_core_parse(frame, sizeof(frame), &observation) == ESP_OK);
  assert(observation.evidence & SPI_WIFI_QUICK_ESPNOW);
  frame[38] = 2;
  assert(wifi_quick_scan_core_parse(frame, sizeof(frame), &observation) == ESP_OK);
  assert(observation.evidence & SPI_WIFI_QUICK_ESPNOW);
  frame[38] = 3;
  assert(wifi_quick_scan_core_parse(frame, sizeof(frame), &observation) == ESP_OK);
  assert(!(observation.evidence & SPI_WIFI_QUICK_ESPNOW));
  frame[38] = 1;
  frame[33] = 250;
  assert(wifi_quick_scan_core_parse(frame, sizeof(frame), &observation) == ESP_OK);
  assert(!(observation.evidence & SPI_WIFI_QUICK_ESPNOW));
  frame[33] = 5;
  frame[22] = 1;
  assert(wifi_quick_scan_core_parse(frame, sizeof(frame), &observation) == ESP_OK);
  assert(!(observation.evidence & SPI_WIFI_QUICK_ESPNOW));
  frame[22] = 0;
  frame[1] = 0x04;
  assert(wifi_quick_scan_core_parse(frame, sizeof(frame), &observation) == ESP_OK);
  assert(!(observation.evidence & SPI_WIFI_QUICK_ESPNOW));
  frame[1] = 0x40;
  assert(wifi_quick_scan_core_parse(frame, sizeof(frame), &observation) == ESP_OK);
  assert(!(observation.evidence & SPI_WIFI_QUICK_ESPNOW));
}

static void test_scoring(void) {
  memset(&core, 0, sizeof(core));
  uint16_t identity = SPI_WIFI_QUICK_ESPRESSIF_OUI | SPI_WIFI_QUICK_ESPNOW;
  observe(esp_mac, 0, -40, identity | SPI_WIFI_QUICK_BEACON);
  observe(esp_mac, 10000, -42, identity | SPI_WIFI_QUICK_PROBE);
  observe(esp_mac, 25000, -41, identity);
  wifi_quick_scan_core_finish(&core, 45000, NULL, 0);
  spi_wifi_quick_record_t *record = &core.entries[0].record;
  assert(core.count == 1 && record->frames == 3);
  assert(record->rssi_avg == -41 && record->rssi_min == -42 && record->rssi_max == -40);
  assert(record->oui_points == 45 && record->fingerprint_points == 25);
  assert(record->proximity_points == 20 && record->persistence_points == 10);
  assert(record->score == 75); // Correlated identity evidence is counted once.
  assert(record->should_investigate && !record->is_authorized);
  assert(record->first_seen_ms == 0 && record->last_seen_ms == 25000);
  assert(record->observation_windows == 3 && !strcmp(record->vendor, "Espressif"));
  assert((record->evidence & (SPI_WIFI_QUICK_BEACON | SPI_WIFI_QUICK_PROBE)) ==
         (SPI_WIFI_QUICK_BEACON | SPI_WIFI_QUICK_PROBE));
  const uint8_t whitelist[1][6] = {{0x24, 0x6F, 0x28, 1, 2, 3}};
  wifi_quick_scan_core_finish(&core, 45000, whitelist, 1);
  assert(record->score == 0 && record->is_authorized && !record->should_investigate);
  memset(&core, 0, sizeof(core));
  // A short burst cannot earn persistence points, even at high packet volume.
  for (unsigned i = 0; i < 100; i++) {
    observe(esp_mac, i, -60, SPI_WIFI_QUICK_ESPRESSIF_OUI);
  }
  wifi_quick_scan_core_finish(&core, 45000, NULL, 0);
  record = &core.entries[0].record;
  assert(record->score == 55 && record->persistence_points == 0);
  observe(esp_mac, 5000, -90, 0);
  wifi_quick_scan_core_finish(&core, 45000, NULL, 0);
  assert(core.entries[0].record.proximity_points == 0);
}

static void test_capacity_and_sort(void) {
  memset(&core, 0, sizeof(core));
  uint8_t mac[6] = {0x02, 0, 0, 0, 0, 0};
  for (unsigned i = 0; i < WIFI_QUICK_SCAN_MAX_RECORDS + 1; i++) {
    mac[5] = i;
    observe(mac, 0, -50, SPI_WIFI_QUICK_LOCAL_MAC);
  }
  assert(core.count == WIFI_QUICK_SCAN_MAX_RECORDS && core.overflow == 1);
  mac[5] = 1;
  observe(mac, 25000, -50, SPI_WIFI_QUICK_ESPNOW);
  wifi_quick_scan_core_finish(&core, 45000, NULL, 0);
  assert(core.entries[0].record.mac[5] == 1);
  assert(core.entries[0].record.score == 25);
  assert(core.entries[0].record.frames == 2);
  assert(!strcmp(core.entries[0].record.vendor, "Local/unknown"));
}

static void test_untrusted_lengths(void) {
  uint32_t seed = 7;
  wifi_quick_scan_core_observation_t observation;
  for (size_t length = 0; length < 512; length++) {
    uint8_t *frame = malloc(length ? length : 1);
    assert(frame != NULL);
    for (unsigned trial = 0; trial < 32; trial++) {
      for (size_t i = 0; i < length; i++) {
        seed = seed * 1664525u + 1013904223u;
        frame[i] = seed >> 24;
      }
      (void)wifi_quick_scan_core_parse(frame, length, &observation);
    }
    free(frame);
  }
}

static void test_network_names(void) {
  uint8_t frame[80] = {0x80};
  uint8_t ap[6] = {0x10, 1, 2, 3, 4, 5};
  memcpy(frame + 10, ap, 6);
  memcpy(frame + 16, ap, 6);
  frame[36] = 0;
  frame[37] = 4;
  memcpy(frame + 38, "Sala", 4);
  wifi_quick_scan_core_observation_t observation;
  assert(wifi_quick_scan_core_parse(frame, 42, &observation) == ESP_OK);
  assert(observation.ssid_state == SPI_WIFI_QUICK_SSID_KNOWN);
  assert(!strcmp(observation.ssid, "Sala"));
  memset(&core, 0, sizeof(core));
  // The client is seen before the beacon; finish resolves its observed BSSID.
  uint8_t data[24] = {0x08, 0x01};
  memcpy(data + 4, ap, 6);
  memcpy(data + 10, esp_mac, 6);
  wifi_quick_scan_core_observation_t client;
  assert(wifi_quick_scan_core_parse(data, sizeof(data), &client) == ESP_OK);
  assert(!memcmp(client.bssid, ap, 6));
  wifi_quick_scan_core_observe(&core, &client);
  wifi_quick_scan_core_observe(&core, &observation);
  wifi_quick_scan_core_finish(&core, 45000, NULL, 0);
  assert(!strcmp(core.entries[0].record.ssid, "Sala"));
  // Neither directed probe requests nor a malformed IE invent an association.
  frame[0] = 0x40;
  assert(wifi_quick_scan_core_parse(frame, 42, &observation) == ESP_OK);
  assert(observation.ssid_state == SPI_WIFI_QUICK_SSID_UNKNOWN);
  static const uint8_t zero[6] = {0};
  assert(!memcmp(observation.bssid, zero, 6));
  frame[0] = 0x80;
  assert(wifi_quick_scan_core_parse(frame, 40, &observation) == ESP_OK);
  assert(observation.ssid_state == SPI_WIFI_QUICK_SSID_UNKNOWN);
  frame[37] = 0;
  assert(wifi_quick_scan_core_parse(frame, 38, &observation) == ESP_OK);
  assert(observation.ssid_state == SPI_WIFI_QUICK_SSID_HIDDEN);
  frame[37] = 4;
  memset(frame + 38, 0, 4);
  assert(wifi_quick_scan_core_parse(frame, 42, &observation) == ESP_OK);
  assert(observation.ssid_state == SPI_WIFI_QUICK_SSID_HIDDEN);
  memcpy(frame + 38, "A\xFF" "B\n", 4);
  frame[0] = 0x50;
  assert(wifi_quick_scan_core_parse(frame, 42, &observation) == ESP_OK);
  assert(!strcmp(observation.ssid, "A?B?"));
  data[1] = 0x02;
  memcpy(data + 10, ap, 6);
  assert(wifi_quick_scan_core_parse(data, sizeof(data), &observation) == ESP_OK);
  assert(!memcmp(observation.bssid, ap, 6));
  data[1] = 0x03;
  assert(wifi_quick_scan_core_parse(data, sizeof(data), &observation) == ESP_OK);
  assert(!memcmp(observation.bssid, zero, 6));
}

int main(void) {
  test_parser();
  test_fingerprint();
  test_scoring();
  test_capacity_and_sort();
  test_untrusted_lengths();
  test_network_names();
  return 0;
}
