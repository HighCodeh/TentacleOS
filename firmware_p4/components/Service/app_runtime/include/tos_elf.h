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

// Minimal relocatable-ELF loader for RISC-V app binaries. Loads a position-
// independent ET_DYN image into an executable PSRAM mapping, applies
// R_RISCV_RELATIVE relocations, and calls its entry as
// app_main(const tos_api_t *api, int argc, char **argv).
//
// v1 runs the app synchronously in the caller's task and tears the mapping down
// when it returns. Supervised task lifecycle is a later phase.

#ifndef TOS_ELF_H
#define TOS_ELF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "tos_api.h"

// A loaded app image. The manager holds this so it can tear the mapping down
// even after force-killing a stuck app task.
typedef struct {
  void *exec_image;  // executable alias (unmap this)
  void *exec_owner;  // writable owner allocation (free)
  void *data_region; // writable data region (free; may be NULL)
  size_t exec_map;   // exec mapping size
  uint32_t entry;    // runtime address of app_main
} tos_elf_image_t;

/**
 * @brief Load and relocate a RISC-V ET_REL app object, leaving it mapped.
 * @return ESP_OK and fills @p out; on failure nothing stays allocated.
 */
esp_err_t tos_elf_load(const uint8_t *elf, size_t elf_len, tos_elf_image_t *out);

/** @brief Call the loaded app's entry as app_main(api, 0, NULL); returns its value. */
int tos_elf_call(const tos_elf_image_t *img, const tos_api_t *api);

/** @brief Unmap and free a loaded image. Safe on a zeroed/failed image. */
void tos_elf_unload(tos_elf_image_t *img);

#ifdef __cplusplus
}
#endif

#endif // TOS_ELF_H
