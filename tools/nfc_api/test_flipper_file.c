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

#include "nfc_flipper_file.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void fixture(const char *path, const char *type, int blocks, int fault) {
  FILE *f = fopen(path, "w");
  assert(f);
  fprintf(f, "# Fixture\r\nFiletype: Flipper NFC device\r\nVersion: 4\r\n"
          "Device type: Mifare Classic\nUID: 04 11 22 33 44 55 66\n"
          "ATQA: 00 44\nSAK: 08\nMifare Classic type: %s\nData format version: 2\n", type);
  for (int block = 0; block < blocks; block++) {
    if (fault == 1 && block == 2) continue;
    fprintf(f, "Block %d:", fault == 2 && block == 2 ? 0 : block);
    for (int byte = 0; byte < 16; byte++) {
      if (fault == 3 && block == 2 && byte == 0) fprintf(f, " ??");
      else fprintf(f, " %02X", (block + byte) & 255);
    }
    fputc('\n', f);
  }
  if (fault == 4) fprintf(f, "Version: 4\n");
  if (fault == 5) fprintf(f, "Block 256: 00\n");
  if (fault == 6) {
    fprintf(f, "# ");
    for (int i = 0; i < 300; i++) fputc('A', f);
    fputc('\n', f);
  }
  assert(fclose(f) == 0);
}

int main(int argc, char **argv) {
  assert(argc == 2);
  mfc_emu_card_data_t card;
  const char *types[] = {"MINI", "1K", "4K"};
  const int sizes[] = {20, 64, 256};
  const int sectors[] = {5, 16, 40};
  for (int t = 0; t < 3; t++) {
    fixture(argv[1], types[t], sizes[t], 0);
    assert(nfc_flipper_file_load_classic(argv[1], &card) == ESP_OK);
    assert(card.total_blocks == sizes[t] && card.sector_count == sectors[t]);
    assert(card.uid_len == 7 && card.uid[6] == 0x66);
    assert(card.atqa[0] == 0x44 && card.atqa[1] == 0);
    for (int s = 0; s < sectors[t]; s++) {
      int trailer = s < 32 ? s * 4 + 3 : 128 + (s - 32) * 16 + 15;
      assert(card.keys[s].key_a[0] == (uint8_t)trailer);
      assert(card.keys[s].key_b[0] == (uint8_t)(trailer + 10));
    }
    assert(nfc_flipper_file_save_classic(argv[1], &card) != ESP_OK);
    assert(remove(argv[1]) == 0);
    assert(nfc_flipper_file_save_classic(argv[1], &card) == ESP_OK);
    mfc_emu_card_data_t roundtrip;
    assert(nfc_flipper_file_load_classic(argv[1], &roundtrip) == ESP_OK);
    assert(!memcmp(&card, &roundtrip, sizeof(card)));
    for (int fault = 1; fault <= 6; fault++) {
      fixture(argv[1], types[t], sizes[t], fault);
      assert(nfc_flipper_file_load_classic(argv[1], &card) != ESP_OK);
      mfc_emu_card_data_t zero = {0};
      assert(!memcmp(&card, &zero, sizeof(card)));
    }
  }
  fixture(argv[1], "1K", 65, 0);
  assert(nfc_flipper_file_load_classic(argv[1], &card) != ESP_OK);
  fixture(argv[1], "1K EV1", 72, 0);
  assert(nfc_flipper_file_load_classic(argv[1], &card) != ESP_OK);
  assert(nfc_flipper_file_load_classic(NULL, &card) == ESP_ERR_INVALID_ARG);
  puts("Classic .nfc: Mini/1K/4K, geometry, UID, ATQA, malformed and incomplete files passed");
}
