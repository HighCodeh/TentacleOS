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

#include "hb_nfc_v_codec.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t patterns[] = {2, 8, 32, 128};

static void feed_byte(hb_nfc_v_codec_rx_t *rx, bool slow, uint8_t value) {
  for (unsigned i = 0; i < (slow ? 64U : 4U); i++) {
    uint8_t samples = slow ? (i == value / 4U ? patterns[value % 4U] : 0) :
        patterns[(value >> (i * 2)) & 3];
    assert(hb_nfc_v_codec_rx_feed(rx, samples) == HB_NFC_V_CODEC_RX_WAIT);
  }
}

static void receive_tests(void) {
  hb_nfc_v_codec_rx_t rx;
  uint8_t data[1024];
  for (unsigned slow = 0; slow < HB_NFC_V_CODEC_RATE_COUNT; slow++) {
    assert(hb_nfc_v_codec_rx_init(&rx, data, sizeof(data)) == ESP_OK);
    assert(hb_nfc_v_codec_rx_feed(&rx, slow ? 0x81 : 0x21) == HB_NFC_V_CODEC_RX_WAIT);
    for (unsigned i = 0; i < sizeof(data); i++) feed_byte(&rx, slow, i & 255);
    assert(hb_nfc_v_codec_rx_feed(&rx, 4) == HB_NFC_V_CODEC_RX_FRAME);
    assert(rx.length == sizeof(data));
    for (unsigned i = 0; i < sizeof(data); i++) assert(data[i] == (i & 255));
    assert(hb_nfc_v_codec_rx_feed(&rx, 0xff) == HB_NFC_V_CODEC_RX_FRAME);
    hb_nfc_v_codec_rx_init(&rx, data, 1);
    hb_nfc_v_codec_rx_feed(&rx, slow ? 0x81 : 0x21);
    feed_byte(&rx, slow, 0xff);
    assert(hb_nfc_v_codec_rx_feed(&rx, 2) == HB_NFC_V_CODEC_RX_OVERFLOW);
    assert(rx.length == 0);
  }
  for (unsigned pattern = 0; pattern < 256; pattern++) {
    if (pattern == 2 || pattern == 8 || pattern == 32 || pattern == 128) continue;
    hb_nfc_v_codec_rx_init(&rx, data, sizeof(data));
    hb_nfc_v_codec_rx_feed(&rx, 0x21);
    assert(hb_nfc_v_codec_rx_feed(&rx, pattern) == HB_NFC_V_CODEC_RX_INVALID);
  }
  hb_nfc_v_codec_rx_init(&rx, data, sizeof(data));
  assert(hb_nfc_v_codec_rx_feed(&rx, 1) == HB_NFC_V_CODEC_RX_SLOT);
  hb_nfc_v_codec_rx_init(&rx, data, sizeof(data));
  hb_nfc_v_codec_rx_feed(&rx, 0x81);
  hb_nfc_v_codec_rx_feed(&rx, 2);
  assert(hb_nfc_v_codec_rx_feed(&rx, 8) == HB_NFC_V_CODEC_RX_INVALID);
  hb_nfc_v_codec_rx_init(&rx, data, sizeof(data));
  hb_nfc_v_codec_rx_feed(&rx, 0x81);
  for (unsigned i = 0; i < 63; i++)
    assert(hb_nfc_v_codec_rx_feed(&rx, 0) == HB_NFC_V_CODEC_RX_WAIT);
  assert(hb_nfc_v_codec_rx_feed(&rx, 0) == HB_NFC_V_CODEC_RX_INVALID);
  hb_nfc_v_codec_rx_init(&rx, data, sizeof(data));
  hb_nfc_v_codec_rx_feed(&rx, 0x21);
  hb_nfc_v_codec_rx_feed(&rx, 2);
  assert(hb_nfc_v_codec_rx_feed(&rx, 4) == HB_NFC_V_CODEC_RX_INVALID);
  assert(hb_nfc_v_codec_rx_init(&rx, NULL, 1) == ESP_ERR_INVALID_ARG);
  assert(hb_nfc_v_codec_rx_feed(&rx, 0x21) == HB_NFC_V_CODEC_RX_INVALID);
}

static uint64_t sample;
static bool previous_level;
static void edge_group(hb_nfc_v_codec_edges_t *edges, uint8_t group) {
  for (unsigned bit = 0; bit < 8; bit++, sample++) {
    bool level = (group >> bit) & 1;
    if (level != previous_level) {
      uint64_t timestamp = 1000 + (sample * 12800 + 678) / 1356;
      hb_nfc_v_codec_edges_feed(edges, timestamp, level);
      previous_level = level;
    }
  }
}
static void edge_tests(void) {
  uint8_t data[256];
  hb_nfc_v_codec_edges_t edges;
  for (unsigned slow = 0; slow < HB_NFC_V_CODEC_RATE_COUNT; slow++) {
    hb_nfc_v_codec_edges_init(&edges, data, sizeof(data));
    sample = 0; previous_level = false;
    edge_group(&edges, slow ? 0x81 : 0x21);
    for (unsigned byte = 0; byte < 256; byte++) {
      for (unsigned group = 0; group < (slow ? 64U : 4U); group++) {
        uint8_t value = slow ? (group == byte / 4 ? patterns[byte % 4] : 0) :
            patterns[(byte >> (2 * group)) & 3];
        edge_group(&edges, value);
      }
    }
    edge_group(&edges, 4);
    uint64_t end = 1000 + (sample * 12800 + 678) / 1356;
    assert(hb_nfc_v_codec_edges_advance(&edges, end) == HB_NFC_V_CODEC_RX_FRAME);
    assert(edges.decoder.length == sizeof(data) && edges.fall_us < end);
    for (unsigned i = 0; i < sizeof(data); i++) assert(data[i] == i);
  }
  hb_nfc_v_codec_edges_init(&edges, data, sizeof(data));
  sample = 0; previous_level = false;
  edge_group(&edges, 1);
  assert(hb_nfc_v_codec_edges_advance(&edges, 1076) == HB_NFC_V_CODEC_RX_SLOT);
  hb_nfc_v_codec_edges_init(&edges, data, sizeof(data));
  hb_nfc_v_codec_edges_feed(&edges, 1000, true);
  assert(hb_nfc_v_codec_edges_feed(&edges, 999, false) == HB_NFC_V_CODEC_RX_INVALID);
}

static void expect_period(hb_nfc_v_codec_tx_t *tx, bool expected_level, uint16_t expected_cycles) {
  bool level;
  uint16_t cycles;
  assert(hb_nfc_v_codec_tx_next(tx, &level, &cycles));
  assert(level == expected_level && cycles == expected_cycles);
}

static void expect_burst(hb_nfc_v_codec_tx_t *tx, unsigned cycles) {
  for (unsigned i = 0; i < cycles / 16; i++) expect_period(tx, !(i & 1), 16);
}

static void transmit_tests(void) {
  hb_nfc_v_codec_tx_t tx;
  const uint8_t frame[] = {0xa1, 0x02};
  for (unsigned slow = 0; slow < HB_NFC_V_CODEC_RATE_COUNT; slow++) {
    unsigned coefficient = (slow & 1) ? 4 : 1;
    unsigned divisor = slow >= HB_NFC_V_CODEC_RATE_FAST_HIGH ? 2 : 1;
    assert(hb_nfc_v_codec_tx_init(&tx, frame, sizeof(frame), slow, false) == ESP_OK);
    expect_burst(&tx, 768 * coefficient / divisor);
    expect_period(&tx, false, 256 * coefficient / divisor);
    expect_burst(&tx, 256 * coefficient / divisor);
    /* A1 02, least-significant bit first. */
    const bool bits[] = {1, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0};
    for (unsigned bit = 0; bit < sizeof(bits); bit++) {
      if (bits[bit]) expect_period(&tx, false, 256 * coefficient / divisor);
      expect_burst(&tx, 256 * coefficient / divisor);
      if (!bits[bit]) expect_period(&tx, false, 256 * coefficient / divisor);
    }
    expect_burst(&tx, 256 * coefficient / divisor);
    expect_period(&tx, false, 256 * coefficient / divisor);
    expect_burst(&tx, 768 * coefficient / divisor);
    bool level;
    uint16_t cycles;
    assert(!hb_nfc_v_codec_tx_next(&tx, &level, &cycles));
    assert(hb_nfc_v_codec_tx_init(&tx, NULL, 0, slow, true) == ESP_OK);
    expect_burst(&tx, 768 * coefficient / divisor);
    expect_period(&tx, false, 256 * coefficient / divisor);
    expect_burst(&tx, 256 * coefficient / divisor);
    assert(!hb_nfc_v_codec_tx_next(&tx, &level, &cycles));
    uint8_t maximum[1024] = {0};
    assert(hb_nfc_v_codec_tx_init(&tx, maximum, sizeof(maximum), slow, false) == ESP_OK);
    uint64_t total_cycles = 0;
    while (hb_nfc_v_codec_tx_next(&tx, &level, &cycles)) total_cycles += cycles;
    assert(total_cycles == (2560ULL + 1024ULL * 8 * 512) * coefficient / divisor);
  }
  assert(hb_nfc_v_codec_tx_init(&tx, frame, 1025, 0, false) == ESP_ERR_INVALID_ARG);
  assert(hb_nfc_v_codec_tx_init(&tx, NULL, 1, 0, false) == ESP_ERR_INVALID_ARG);
  assert(hb_nfc_v_codec_tx_init(&tx, frame, 1, HB_NFC_V_CODEC_RATE_COUNT, false) == ESP_ERR_INVALID_ARG);
}

int main(void) {
  receive_tests();
  edge_tests();
  transmit_tests();
  puts("NFC-V codec: all byte values, 1-of-4/256, framing, bounds, high/low response waveforms passed");
}
