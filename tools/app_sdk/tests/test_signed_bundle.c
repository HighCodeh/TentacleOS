#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tos_hb.h"

int main(int argc, char **argv) {
  assert(argc == 3);
  FILE *file = fopen(argv[1], "rb");
  assert(file != NULL);
  unsigned char buffer[4096];
  size_t size = fread(buffer, 1, sizeof(buffer), file);
  assert(!ferror(file) && feof(file));
  assert(fclose(file) == 0);
  tos_hb_t bundle = {0};
  esp_err_t error = tos_hb_open(buffer, size, &bundle);
  assert(error == (esp_err_t)strtol(argv[2], NULL, 10));
  if (error == ESP_OK) {
    assert(bundle.abi_major == 1 && bundle.abi_minor == 15 && bundle.caps == 8);
    assert(strcmp(bundle.name, "radio-test") == 0);
    const unsigned char elf[] = "ELF\0test payload";
    assert(bundle.elf_len == sizeof(elf) && memcmp(bundle.elf, elf, sizeof(elf)) == 0);
    if (bundle.title != NULL) {
      assert(bundle.title_len == 5 && memcmp(bundle.title, "radio", 5) == 0);
      assert(bundle.icon_len == 4 && memcmp(bundle.icon, "ICON", 4) == 0);
    }
  }
  return 0;
}
