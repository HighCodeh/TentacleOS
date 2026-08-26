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

// Split loader for RISC-V ET_REL app objects. Because the P4 MMU forbids EXEC
// together with WRITE, code and read-only data go into an executable PSRAM
// region (loaded through a writable alias, run through an exec alias) while
// writable data (.data/.bss) goes into a separate R/W region. Each relocation is
// patched with the real runtime address of its target, so code can reach data
// across the two regions. Entry is the app_main symbol.

#include "tos_elf.h"

#include <string.h>

#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mmu_map.h"
#include "sdkconfig.h"

static const char *TAG = "TOS_ELF";

typedef struct {
  uint8_t e_ident[16];
  uint16_t e_type, e_machine;
  uint32_t e_version, e_entry, e_phoff, e_shoff, e_flags;
  uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} elf32_ehdr_t;

typedef struct {
  uint32_t sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size;
  uint32_t sh_link, sh_info, sh_addralign, sh_entsize;
} elf32_shdr_t;

typedef struct {
  uint32_t st_name, st_value, st_size;
  uint8_t st_info, st_other;
  uint16_t st_shndx;
} elf32_sym_t;

typedef struct {
  uint32_t r_offset, r_info;
  int32_t r_addend;
} elf32_rela_t;

#define ET_REL           1
#define EM_RISCV         243
#define SHT_PROGBITS     1
#define SHT_SYMTAB       2
#define SHT_RELA         4
#define SHT_NOBITS       8
#define SHF_WRITE        0x1
#define SHF_ALLOC        0x2
#define SHF_EXECINSTR    0x4
#define SHN_UNDEF        0
#define SHN_ABS          0xfff1

#define R_RISCV_32       1
#define R_RISCV_BRANCH   16
#define R_RISCV_JAL      17
#define R_RISCV_CALL     18
#define R_RISCV_CALL_PLT 19
#define R_RISCV_HI20     26
#define R_RISCV_LO12_I   27
#define R_RISCV_LO12_S   28
#define R_RISCV_ALIGN    43
#define R_RISCV_RELAX    51

#define ELF32_R_SYM(i)  ((i) >> 8)
#define ELF32_R_TYPE(i) ((i) & 0xff)
#define ELF32_ST_TYPE(i) ((i) & 0xf)
#define STT_SECTION 3

#define REGION_EXEC 0
#define REGION_DATA 1
#define MAX_SECTIONS 96
#define ALIGN_UP(v, a) (((v) + ((a) - 1)) & ~((size_t)(a) - 1))

typedef int (*app_entry_fn)(const tos_api_t *api, int argc, char **argv);

typedef struct {
  uint8_t placed; // 1 if this section got a runtime slot
  uint8_t region; // REGION_EXEC or REGION_DATA
  uint32_t off;   // byte offset within its region
} sec_meta_t;

typedef struct {
  const uint8_t *elf;
  const elf32_shdr_t *sh;
  uint16_t shnum;
  sec_meta_t meta[MAX_SECTIONS];
  // exec region: written through owner, run through the exec alias.
  uint32_t exec_run, exec_write;
  // data region: one R/W mapping (run == write).
  uint32_t data_run;
} loader_t;

static uint32_t sec_run(const loader_t *l, uint16_t i) {
  return (l->meta[i].region == REGION_EXEC ? l->exec_run : l->data_run) + l->meta[i].off;
}
static uint32_t sec_write(const loader_t *l, uint16_t i) {
  return (l->meta[i].region == REGION_EXEC ? l->exec_write : l->data_run) + l->meta[i].off;
}

static uint32_t sym_run_addr(const loader_t *l, const elf32_sym_t *s) {
  if (s->st_shndx == SHN_ABS)
    return s->st_value;
  if (s->st_shndx == SHN_UNDEF || s->st_shndx >= l->shnum || !l->meta[s->st_shndx].placed)
    return 0;
  return sec_run(l, s->st_shndx) + s->st_value;
}

static void patch_u32(uint32_t addr, uint32_t v) {
  memcpy((void *)(uintptr_t)addr, &v, 4);
}
static uint32_t read_u32(uint32_t addr) {
  uint32_t v;
  memcpy(&v, (void *)(uintptr_t)addr, 4);
  return v;
}

// I-type immediate (bits 31:20).
static void patch_lo12_i(uint32_t at, uint32_t val) {
  uint32_t insn = read_u32(at);
  insn = (insn & 0x000fffff) | ((val & 0xfff) << 20);
  patch_u32(at, insn);
}
// S-type immediate (bits 31:25 = imm[11:5], 11:7 = imm[4:0]).
static void patch_lo12_s(uint32_t at, uint32_t val) {
  uint32_t insn = read_u32(at);
  insn = (insn & 0x01fff07f) | ((val & 0xfe0) << 20) | ((val & 0x1f) << 7);
  patch_u32(at, insn);
}
// U-type high 20 bits (lui/auipc); +0x800 rounds for the paired LO12 sign.
static void patch_hi20(uint32_t at, uint32_t val) {
  uint32_t insn = read_u32(at);
  insn = (insn & 0x00000fff) | ((val + 0x800) & 0xfffff000);
  patch_u32(at, insn);
}

static esp_err_t apply_one_rela(loader_t *l,
                                const elf32_shdr_t *symsh,
                                const elf32_rela_t *r,
                                uint32_t p_write,
                                uint32_t p_run) {
  const elf32_sym_t *symtab = (const elf32_sym_t *)(l->elf + symsh->sh_offset);
  const elf32_sym_t *sym = &symtab[ELF32_R_SYM(r->r_info)];
  uint32_t s = sym_run_addr(l, sym) + (uint32_t)r->r_addend;
  uint32_t type = ELF32_R_TYPE(r->r_info);

  switch (type) {
    case R_RISCV_32:
      patch_u32(p_write, s);
      return ESP_OK;
    case R_RISCV_HI20:
      patch_hi20(p_write, s);
      return ESP_OK;
    case R_RISCV_LO12_I:
      patch_lo12_i(p_write, s);
      return ESP_OK;
    case R_RISCV_LO12_S:
      patch_lo12_s(p_write, s);
      return ESP_OK;
    case R_RISCV_CALL:
    case R_RISCV_CALL_PLT: {
      uint32_t off = s - p_run; // auipc + jalr pair
      patch_hi20(p_write, off);
      patch_lo12_i(p_write + 4, off);
      return ESP_OK;
    }
    case R_RISCV_JAL: {
      uint32_t off = s - p_run;
      uint32_t insn = read_u32(p_write) & 0x00000fff;
      insn |= ((off >> 12) & 0xff) << 12;
      insn |= ((off >> 11) & 0x1) << 20;
      insn |= ((off >> 1) & 0x3ff) << 21;
      insn |= ((off >> 20) & 0x1) << 31;
      patch_u32(p_write, insn);
      return ESP_OK;
    }
    case R_RISCV_BRANCH: {
      uint32_t off = s - p_run;
      uint32_t insn = read_u32(p_write) & 0x01fff07f;
      insn |= ((off >> 11) & 0x1) << 7;
      insn |= ((off >> 1) & 0xf) << 8;
      insn |= ((off >> 5) & 0x3f) << 25;
      insn |= ((off >> 12) & 0x1) << 31;
      patch_u32(p_write, insn);
      return ESP_OK;
    }
    case R_RISCV_ALIGN:
    case R_RISCV_RELAX:
      return ESP_OK; // no-op (built with -mno-relax)
    default:
      ESP_LOGE(TAG, "unsupported relocation type %u", (unsigned)type);
      return ESP_ERR_NOT_SUPPORTED;
  }
}

esp_err_t tos_elf_load(const uint8_t *elf, size_t elf_len, tos_elf_image_t *out) {
  if (elf == NULL || out == NULL || elf_len < sizeof(elf32_ehdr_t))
    return ESP_ERR_INVALID_ARG;

  const elf32_ehdr_t *eh = (const elf32_ehdr_t *)elf;
  if (memcmp(eh->e_ident, "\x7f""ELF", 4) != 0 || eh->e_ident[4] != 1 || eh->e_ident[5] != 1) {
    ESP_LOGE(TAG, "not a little-endian 32-bit ELF");
    return ESP_ERR_NOT_SUPPORTED;
  }
  if (eh->e_machine != EM_RISCV || eh->e_type != ET_REL) {
    ESP_LOGE(TAG, "need a RISC-V ET_REL object (machine=%u type=%u)", eh->e_machine, eh->e_type);
    return ESP_ERR_NOT_SUPPORTED;
  }
  if (eh->e_shnum > MAX_SECTIONS ||
      (size_t)eh->e_shoff + (size_t)eh->e_shnum * eh->e_shentsize > elf_len) {
    ESP_LOGE(TAG, "bad/too many section headers");
    return ESP_ERR_INVALID_SIZE;
  }

  loader_t l = {0};
  l.elf = elf;
  l.sh = (const elf32_shdr_t *)(elf + eh->e_shoff);
  l.shnum = eh->e_shnum;

  // Placement pass: pack alloc sections into the exec and data regions.
  size_t exec_total = 0, data_total = 0;
  const elf32_shdr_t *symsh = NULL;
  for (uint16_t i = 0; i < l.shnum; i++) {
    const elf32_shdr_t *s = &l.sh[i];
    if (s->sh_type == SHT_SYMTAB)
      symsh = s;
    if (!(s->sh_flags & SHF_ALLOC) || s->sh_size == 0)
      continue;
    size_t align = s->sh_addralign ? s->sh_addralign : 1;
    if (s->sh_flags & SHF_WRITE) {
      data_total = ALIGN_UP(data_total, align);
      l.meta[i] = (sec_meta_t){.placed = 1, .region = REGION_DATA, .off = (uint32_t)data_total};
      data_total += s->sh_size;
    } else { // code and read-only data
      exec_total = ALIGN_UP(exec_total, align);
      l.meta[i] = (sec_meta_t){.placed = 1, .region = REGION_EXEC, .off = (uint32_t)exec_total};
      exec_total += s->sh_size;
    }
  }
  if (symsh == NULL || exec_total == 0) {
    ESP_LOGE(TAG, "no symbol table or no code");
    return ESP_ERR_NOT_SUPPORTED;
  }

  const size_t page = CONFIG_MMU_PAGE_SIZE;
  size_t exec_map = ALIGN_UP(exec_total, page);

  esp_err_t err;
  uint8_t *exec_owner = NULL, *data_region = NULL;
  void *exec_image = NULL;
  esp_paddr_t paddr = 0;
  mmu_target_t tgt = 0;

  exec_owner = heap_caps_aligned_alloc(page, exec_map, MALLOC_CAP_SPIRAM);
  if (exec_owner == NULL)
    return ESP_ERR_NO_MEM;
  if (data_total > 0) {
    data_region = heap_caps_malloc(data_total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (data_region == NULL) {
      err = ESP_ERR_NO_MEM;
      goto out;
    }
  }

  err = esp_mmu_vaddr_to_paddr(exec_owner, &paddr, &tgt);
  if (err != ESP_OK)
    goto out;
  err = esp_mmu_map(paddr, exec_map, MMU_TARGET_PSRAM0, MMU_MEM_CAP_EXEC | MMU_MEM_CAP_READ,
                    ESP_MMU_MMAP_FLAG_PADDR_SHARED, &exec_image);
  if (err != ESP_OK || exec_image == NULL) {
    ESP_LOGE(TAG, "esp_mmu_map exec alias: %s", esp_err_to_name(err));
    goto out;
  }

  l.exec_write = (uint32_t)(uintptr_t)exec_owner;
  l.exec_run = (uint32_t)(uintptr_t)exec_image;
  l.data_run = (uint32_t)(uintptr_t)data_region;

  // Load pass (into the writable views).
  for (uint16_t i = 0; i < l.shnum; i++) {
    if (!l.meta[i].placed)
      continue;
    const elf32_shdr_t *s = &l.sh[i];
    uint8_t *dst = (uint8_t *)(uintptr_t)sec_write(&l, i);
    if (s->sh_type == SHT_NOBITS)
      memset(dst, 0, s->sh_size);
    else
      memcpy(dst, elf + s->sh_offset, s->sh_size);
  }

  // Relocation pass.
  for (uint16_t i = 0; i < l.shnum; i++) {
    const elf32_shdr_t *rs = &l.sh[i];
    if (rs->sh_type != SHT_RELA)
      continue;
    uint16_t target = rs->sh_info;
    if (target >= l.shnum || !l.meta[target].placed)
      continue;
    const elf32_shdr_t *tsym = &l.sh[rs->sh_link]; // symtab for this rela
    uint32_t twrite = sec_write(&l, target);
    uint32_t trun = sec_run(&l, target);
    uint32_t n = rs->sh_entsize ? (rs->sh_size / rs->sh_entsize) : 0;
    const elf32_rela_t *tbl = (const elf32_rela_t *)(elf + rs->sh_offset);
    for (uint32_t k = 0; k < n; k++) {
      const elf32_rela_t *r = &tbl[k];
      err = apply_one_rela(&l, tsym, r, twrite + r->r_offset, trun + r->r_offset);
      if (err != ESP_OK)
        goto out_unmap;
    }
  }

  // Resolve app_main from the symbol table.
  uint32_t entry_addr = 0;
  {
    const elf32_sym_t *symtab = (const elf32_sym_t *)(elf + symsh->sh_offset);
    const char *strtab = (const char *)(elf + l.sh[symsh->sh_link].sh_offset);
    uint32_t nsym = symsh->sh_entsize ? (symsh->sh_size / symsh->sh_entsize) : 0;
    for (uint32_t k = 0; k < nsym; k++) {
      if (strcmp(strtab + symtab[k].st_name, "app_main") == 0) {
        entry_addr = sym_run_addr(&l, &symtab[k]);
        break;
      }
    }
  }
  if (entry_addr == 0) {
    ESP_LOGE(TAG, "app_main not found");
    err = ESP_ERR_NOT_FOUND;
    goto out_unmap;
  }

  // Publish the exec region's writes and drop stale instruction-cache lines.
  esp_cache_msync(exec_owner, exec_map, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  esp_cache_msync(exec_image, exec_map, ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_TYPE_INST);

  out->exec_image = exec_image;
  out->exec_owner = exec_owner;
  out->data_region = data_region;
  out->exec_map = exec_map;
  out->entry = entry_addr;
  ESP_LOGI(TAG, "loaded app: exec=%p data=%p entry=0x%08x", exec_image, data_region,
           (unsigned)entry_addr);
  return ESP_OK;

out_unmap:
  esp_mmu_unmap(exec_image);
out:
  if (data_region != NULL)
    heap_caps_free(data_region);
  heap_caps_free(exec_owner);
  return err;
}

int tos_elf_call(const tos_elf_image_t *img, const tos_api_t *api) {
  return ((app_entry_fn)(uintptr_t)img->entry)(api, 0, NULL);
}

void tos_elf_unload(tos_elf_image_t *img) {
  if (img == NULL)
    return;
  if (img->exec_image != NULL)
    esp_mmu_unmap(img->exec_image);
  if (img->data_region != NULL)
    heap_caps_free(img->data_region);
  if (img->exec_owner != NULL)
    heap_caps_free(img->exec_owner);
  memset(img, 0, sizeof(*img));
}
