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

#include "ir_store.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "ir_protocol.h"
#include "storage_mkdir.h"
#include "tos_storage_paths.h"

static const char *TAG = "IR_STORE";

#define IR_DIR            TOS_PATH_IR // "/sdcard/ir"
#define IR_STORE_FILE_MAX (512 * 1024)
#define IR_SERIALIZE_MAX  16384
#define IR_CAP_TASK_STACK 8192
#define IR_CAP_TASK_PRIO  SYS_PRIO_SERVICE_HI

// ----------------------------------------------------------------------------
// Path helpers
// ----------------------------------------------------------------------------

static void path_for(const char *name, char *out, size_t cap) {
  snprintf(out, cap, "%s/%s.ir", IR_DIR, name);
}

bool ir_store_exists(const char *name) {
  if (name == NULL || name[0] == '\0')
    return false;
  char path[IR_STORE_PATH_MAX];
  path_for(name, path, sizeof(path));
  struct stat st;
  return stat(path, &st) == 0;
}

// Read the first block of a file and pull out the protocol name (or "RAW") so
// list rows can show it without paying for a full parse of every file.
static void sniff_proto(const char *path, char *out, size_t cap) {
  out[0] = '\0';
  FILE *f = fopen(path, "rb");
  if (f == NULL)
    return;
  char buf[512];
  size_t n = fread(buf, 1, sizeof(buf) - 1, f);
  fclose(f);
  buf[n] = '\0';

  const char *p = strstr(buf, "protocol:");
  if (p != NULL) {
    p += strlen("protocol:");
    while (*p == ' ' || *p == '\t')
      p++;
    size_t i = 0;
    while (*p != '\0' && *p != '\r' && *p != '\n' && i < cap - 1)
      out[i++] = *p++;
    out[i] = '\0';
    return;
  }
  if (strstr(buf, "type: raw") != NULL || strstr(buf, "type:raw") != NULL) {
    snprintf(out, cap, "RAW");
    return;
  }
  snprintf(out, cap, "IR");
}

// ----------------------------------------------------------------------------
// Listing
// ----------------------------------------------------------------------------

int ir_store_list(ir_store_entry_t *out, int max) {
  if (out == NULL || max <= 0)
    return -1;

  DIR *dir = opendir(IR_DIR);
  if (dir == NULL)
    return 0; // dir not created yet == no saved signals

  int n = 0;
  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL && n < max) {
    const char *nm = ent->d_name;
    size_t len = strlen(nm);
    if (len < 4 || strcasecmp(nm + len - 3, ".ir") != 0)
      continue;

    ir_store_entry_t *e = &out[n];
    size_t stem = len - 3;
    if (stem >= IR_STORE_NAME_MAX)
      stem = IR_STORE_NAME_MAX - 1;
    memcpy(e->name, nm, stem);
    e->name[stem] = '\0';
    // Rebuild the path from the bounded stem (char[32]) rather than d_name
    // (char[256]) so the length is provable to the compiler; the SD FAT is
    // case-insensitive, so this still opens the real file.
    snprintf(e->path, sizeof(e->path), "%s/%s.ir", IR_DIR, e->name);
    sniff_proto(e->path, e->proto, sizeof(e->proto));
    n++;
  }
  closedir(dir);
  return n;
}

// ----------------------------------------------------------------------------
// Load / save / delete / rename
// ----------------------------------------------------------------------------

esp_err_t ir_store_load(const char *path, ir_file_t *file) {
  if (path == NULL || file == NULL)
    return ESP_ERR_INVALID_ARG;

  FILE *f = fopen(path, "rb");
  if (f == NULL)
    return ESP_ERR_NOT_FOUND;

  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz <= 0 || sz > IR_STORE_FILE_MAX) {
    fclose(f);
    return ESP_ERR_INVALID_SIZE;
  }

  char *buf = heap_caps_malloc((size_t)sz + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (buf == NULL) {
    fclose(f);
    return ESP_ERR_NO_MEM;
  }
  size_t rd = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  buf[rd] = '\0';

  esp_err_t r = ir_file_parse(buf, file);
  free(buf);
  return r;
}

static esp_err_t write_file(const ir_file_t *file, const char *name) {
  storage_mkdir_recursive(IR_DIR);

  char *buf = malloc(IR_SERIALIZE_MAX);
  if (buf == NULL)
    return ESP_ERR_NO_MEM;
  size_t n = ir_file_to_string(file, buf, IR_SERIALIZE_MAX);

  char path[IR_STORE_PATH_MAX];
  path_for(name, path, sizeof(path));
  FILE *f = fopen(path, "wb");
  if (f == NULL) {
    free(buf);
    ESP_LOGE(TAG, "open %s for write failed", path);
    return ESP_FAIL;
  }
  size_t wr = fwrite(buf, 1, n, f);
  fclose(f);
  free(buf);
  return wr == n ? ESP_OK : ESP_FAIL;
}

esp_err_t ir_store_save_data(const char *name, const ir_data_t *data) {
  if (name == NULL || data == NULL)
    return ESP_ERR_INVALID_ARG;

  ir_file_t file;
  ir_file_init(&file);
  esp_err_t r = ir_file_add_parsed(&file, name, data);
  if (r == ESP_OK)
    r = write_file(&file, name);
  ir_file_free(&file);
  if (r == ESP_OK)
    ESP_LOGI(TAG, "saved %s.ir", name);
  return r;
}

esp_err_t
ir_store_save_raw(const char *name, const rmt_symbol_word_t *symbols, size_t count, uint32_t freq) {
  if (name == NULL || symbols == NULL || count == 0)
    return ESP_ERR_INVALID_ARG;

  ir_file_t file;
  ir_file_init(&file);
  ir_file_add_raw_cfg_t cfg = {
      .name = name,
      .symbols = symbols,
      .count = count,
      .freq = freq,
  };
  esp_err_t r = ir_file_add_raw(&file, &cfg);
  if (r == ESP_OK)
    r = write_file(&file, name);
  ir_file_free(&file);
  if (r == ESP_OK)
    ESP_LOGI(TAG, "saved %s.ir (raw, %u symbols)", name, (unsigned)count);
  return r;
}

esp_err_t ir_store_delete(const char *name) {
  if (name == NULL)
    return ESP_ERR_INVALID_ARG;
  char path[IR_STORE_PATH_MAX];
  path_for(name, path, sizeof(path));
  return remove(path) == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t ir_store_rename(const char *old_name, const char *new_name) {
  if (old_name == NULL || new_name == NULL)
    return ESP_ERR_INVALID_ARG;
  char op[IR_STORE_PATH_MAX];
  char np[IR_STORE_PATH_MAX];
  path_for(old_name, op, sizeof(op));
  path_for(new_name, np, sizeof(np));
  return rename(op, np) == 0 ? ESP_OK : ESP_FAIL;
}

void ir_store_name_for_data(const ir_data_t *data, char *buf, size_t cap) {
  if (buf == NULL || cap == 0)
    return;
  if (data == NULL) {
    snprintf(buf, cap, "signal");
    return;
  }
  snprintf(buf,
           cap,
           "%s_%02lX_%02lX",
           ir_protocol_name(data->protocol),
           (unsigned long)data->address,
           (unsigned long)data->command);
}

void ir_store_next_free(const char *prefix, char *buf, size_t cap) {
  if (buf == NULL || cap == 0)
    return;
  const char *pfx = (prefix != NULL && prefix[0] != '\0') ? prefix : "signal";
  for (int i = 1; i < 1000; i++) {
    snprintf(buf, cap, "%s_%03d", pfx, i);
    if (!ir_store_exists(buf))
      return;
  }
  snprintf(buf, cap, "%s_new", pfx);
}

// ----------------------------------------------------------------------------
// Transmit (blocking; lazily brings TX up)
// ----------------------------------------------------------------------------

// Bring TX up, logging loudly on failure (a swallowed ir_tx_init error looks
// exactly like "the LEDs never blink").
static esp_err_t tx_ready(void) {
  esp_err_t r = ir_tx_init();
  if (r != ESP_OK)
    ESP_LOGE(TAG, "ir_tx_init failed: %s", esp_err_to_name(r));
  return r;
}

esp_err_t ir_store_send_data(const ir_data_t *data) {
  if (data == NULL)
    return ESP_ERR_INVALID_ARG;
  esp_err_t r = tx_ready();
  if (r != ESP_OK)
    return r;
  r = ir_send(data);
  ESP_LOGI(TAG,
           "send %s 0x%lX/0x%lX -> %s",
           ir_protocol_name(data->protocol),
           (unsigned long)data->address,
           (unsigned long)data->command,
           esp_err_to_name(r));
  return r;
}

esp_err_t ir_store_send_signal(const ir_signal_t *signal) {
  if (signal == NULL)
    return ESP_ERR_INVALID_ARG;
  esp_err_t r = tx_ready();
  if (r != ESP_OK)
    return r;
  r = ir_file_send(signal);
  ESP_LOGI(TAG,
           "send '%s' (%s) -> %s",
           signal->name,
           signal->is_raw ? "raw" : ir_protocol_name(signal->data.protocol),
           esp_err_to_name(r));
  return r;
}

esp_err_t ir_store_send_raw(const rmt_symbol_word_t *symbols, size_t count, uint32_t freq) {
  if (symbols == NULL || count == 0)
    return ESP_ERR_INVALID_ARG;
  esp_err_t r = tx_ready();
  if (r != ESP_OK)
    return r;
  r = ir_send_raw(symbols, count, freq);
  ESP_LOGI(TAG,
           "send raw %u sym @%luHz -> %s",
           (unsigned)count,
           (unsigned long)freq,
           esp_err_to_name(r));
  return r;
}

int ir_store_send_named(const ir_file_t *file, const char *name) {
  if (file == NULL || name == NULL)
    return 0;
  if (tx_ready() != ESP_OK)
    return 0;
  int sent = 0;
  for (size_t i = 0; i < file->count; i++) {
    if (strcasecmp(file->signals[i].name, name) == 0) {
      if (ir_file_send(&file->signals[i]) == ESP_OK)
        sent++;
    }
  }
  ESP_LOGI(TAG, "send named '%s' -> %d code(s)", name, sent);
  return sent;
}

// ----------------------------------------------------------------------------
// Background RX capture (ir_receive blocks; UI thread polls a 1-slot queue)
// ----------------------------------------------------------------------------

typedef struct {
  esp_err_t status;
  ir_data_t data;
} cap_msg_t;

static QueueHandle_t s_cap_q = NULL;
static volatile bool s_cap_running = false;
static ir_cap_status_t s_cap_state = IR_CAP_IDLE;
static ir_data_t s_cap_data = {0};

static void capture_task(void *arg) {
  uint32_t timeout_ms = (uint32_t)(uintptr_t)arg;
  cap_msg_t msg = {0};

  if (ir_rx_init() != ESP_OK) {
    msg.status = ESP_FAIL;
  } else {
    ir_rx_prime(); // drop stale frame + re-arm if a frame left the channel idle
    msg.status = ir_receive(&msg.data, timeout_ms);
  }

  xQueueOverwrite(s_cap_q, &msg);
  s_cap_running = false;
  vTaskDelete(NULL);
}

esp_err_t ir_capture_start(uint32_t timeout_ms) {
  if (s_cap_running)
    return ESP_ERR_INVALID_STATE;

  if (s_cap_q == NULL) {
    s_cap_q = xQueueCreate(1, sizeof(cap_msg_t));
    if (s_cap_q == NULL)
      return ESP_ERR_NO_MEM;
  }
  xQueueReset(s_cap_q);

  s_cap_state = IR_CAP_BUSY;
  s_cap_running = true;
  if (xTaskCreatePinnedToCore(capture_task,
                              "ir_capture",
                              IR_CAP_TASK_STACK,
                              (void *)(uintptr_t)timeout_ms,
                              IR_CAP_TASK_PRIO,
                              NULL,
                              SYS_CORE_RADIO) != pdPASS) {
    s_cap_running = false;
    s_cap_state = IR_CAP_ERROR;
    return ESP_FAIL;
  }
  return ESP_OK;
}

ir_cap_status_t ir_capture_poll(ir_data_t *out) {
  if (s_cap_state == IR_CAP_BUSY && s_cap_q != NULL) {
    cap_msg_t msg;
    if (xQueueReceive(s_cap_q, &msg, 0) == pdPASS) {
      if (msg.status == ESP_OK) {
        s_cap_data = msg.data;
        s_cap_state = IR_CAP_GOT;
      } else if (msg.status == ESP_ERR_NOT_FOUND) {
        // A frame arrived but no protocol matched — raw is still available via
        // ir_get_last_raw(); present it as an unknown signal.
        s_cap_data = (ir_data_t){0};
        s_cap_state = IR_CAP_GOT;
      } else if (msg.status == ESP_ERR_TIMEOUT) {
        s_cap_state = IR_CAP_TIMEOUT;
      } else {
        s_cap_state = IR_CAP_ERROR;
      }
    }
  }
  if (s_cap_state == IR_CAP_GOT && out != NULL)
    *out = s_cap_data;
  return s_cap_state;
}

bool ir_capture_decoded(void) {
  return s_cap_state == IR_CAP_GOT && s_cap_data.protocol != IR_PROTO_UNKNOWN;
}

void ir_capture_reset(void) {
  if (s_cap_running)
    return; // let the in-flight task finish; start() is guarded anyway
  s_cap_state = IR_CAP_IDLE;
  s_cap_data = (ir_data_t){0};
  if (s_cap_q != NULL)
    xQueueReset(s_cap_q);
}
