// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "ir_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <esp_log.h>

static const char *TAG = "IR_FILE";

// ══════════════════════════════════════════════════════
// Init / Free
// ══════════════════════════════════════════════════════

void ir_file_init(ir_file_t *file) {
    file->signals = NULL;
    file->count = 0;
    file->capacity = 0;
}

void ir_file_free(ir_file_t *file) {
    for (size_t i = 0; i < file->count; i++) {
        free(file->signals[i].raw);
    }
    free(file->signals);
    file->signals = NULL;
    file->count = 0;
    file->capacity = 0;
}

static void ir_file_grow(ir_file_t *file) {
    if (file->count >= file->capacity) {
        size_t new_cap = file->capacity == 0 ? 8 : file->capacity * 2;
        file->signals = realloc(file->signals, new_cap * sizeof(ir_signal_t));
        file->capacity = new_cap;
    }
}

// ══════════════════════════════════════════════════════
// Flipper ↔ ir_data_t mapping
// ══════════════════════════════════════════════════════

// "04 00 00 00" -> uint32_t little-endian
static uint32_t parse_hex_bytes(const char *str) {
    uint32_t result = 0;
    int shift = 0;
    while (*str && shift < 32) {
        while (*str == ' ') str++;
        if (*str == '\0') break;
        unsigned int byte;
        if (sscanf(str, "%2x", &byte) != 1) break;
        result |= ((uint32_t)byte << shift);
        shift += 8;
        str += 2;
    }
    return result;
}

static bool flipper_to_ir_data(const char *proto, uint32_t addr, uint32_t cmd, ir_data_t *out) {
    memset(out, 0, sizeof(ir_data_t));

    if (strcmp(proto, "NEC") == 0 || strcmp(proto, "NECext") == 0 ||
        strcmp(proto, "NEC42") == 0 || strcmp(proto, "NEC42ext") == 0) {
        out->protocol = IR_PROTO_NEC;
        out->address = addr & 0xFFFF;
        out->command = cmd & 0xFF;
        return true;
    }
    if (strcmp(proto, "Samsung32") == 0) {
        out->protocol = IR_PROTO_SAMSUNG;
        out->address = addr & 0xFFFF;
        out->command = cmd & 0xFF;
        return true;
    }
    if (strcmp(proto, "RC6") == 0) {
        out->protocol = IR_PROTO_RC6;
        out->address = addr & 0xFF;
        out->command = cmd & 0xFF;
        return true;
    }
    if (strcmp(proto, "RC5") == 0 || strcmp(proto, "RC5X") == 0) {
        out->protocol = IR_PROTO_RC5;
        out->address = addr & 0x1F;
        out->command = cmd & 0x7F;
        return true;
    }
    if (strcmp(proto, "SIRC") == 0 || strcmp(proto, "SIRC15") == 0 || strcmp(proto, "SIRC20") == 0) {
        out->protocol = IR_PROTO_SONY;
        out->address = addr & 0x1FFF;
        out->command = cmd & 0x7F;
        return true;
    }
    if (strcmp(proto, "Kaseikyo") == 0) {
        out->protocol = IR_PROTO_PANASONIC;
        out->address = (addr >> 16) & 0xFFF;
        out->command = cmd & 0xFF;
        return true;
    }

    return false;
}

static const char *ir_to_flipper_proto(ir_protocol_t proto, uint16_t address, uint16_t command) {
    switch (proto) {
        case IR_PROTO_NEC:       return (address > 0xFF) ? "NECext" : "NEC";
        case IR_PROTO_SAMSUNG:   return "Samsung32";
        case IR_PROTO_RC6:       return "RC6";
        case IR_PROTO_RC5:       return (command > 0x3F) ? "RC5X" : "RC5";
        case IR_PROTO_PANASONIC: return "Kaseikyo";
        case IR_PROTO_SONY:
            if (address > 0xFF)      return "SIRC20";
            else if (address > 0x1F) return "SIRC15";
            else                     return "SIRC";
        default: return NULL;  // no Flipper equivalent, use raw
    }
}

// ══════════════════════════════════════════════════════
// Parser
// ══════════════════════════════════════════════════════

static void parse_raw_data(const char *str, ir_signal_t *sig) {
    // Count values
    size_t count = 0;
    const char *p = str;
    while (*p) {
        while (*p == ' ') p++;
        if (*p == '\0') break;
        count++;
        while (*p && *p != ' ') p++;
    }

    sig->raw = malloc(count * sizeof(uint32_t));
    sig->raw_count = 0;
    if (sig->raw == NULL) return;

    p = str;
    while (*p) {
        while (*p == ' ') p++;
        if (*p == '\0') break;
        sig->raw[sig->raw_count++] = (uint32_t)atol(p);
        while (*p && *p != ' ') p++;
    }
}

static void finalize_signal(ir_signal_t *current, const char *proto_name,
                            uint32_t addr32, uint32_t cmd32, ir_file_t *file) {
    if (!current->is_raw) {
        if (!flipper_to_ir_data(proto_name, addr32, cmd32, &current->data)) {
            ESP_LOGW(TAG, "Unknown protocol '%s' for signal '%s'", proto_name, current->name);
            return;
        }
    }
    ir_file_grow(file);
    file->signals[file->count++] = *current;
}

bool ir_file_parse(const char *content, ir_file_t *file) {
    const char *line = content;
    ir_signal_t current;
    memset(&current, 0, sizeof(current));
    bool in_signal = false;
    char proto_name[32] = {};
    uint32_t addr32 = 0, cmd32 = 0;

    while (*line) {
        const char *eol = strchr(line, '\n');
        size_t len = eol ? (size_t)(eol - line) : strlen(line);

        char buf[1024];
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        memcpy(buf, line, len);
        buf[len] = '\0';
        if (len > 0 && buf[len - 1] == '\r') buf[--len] = '\0';

        line = eol ? eol + 1 : line + len;

        // Skip comments, empty lines and header
        if (buf[0] == '#' || len == 0) continue;
        if (strncmp(buf, "Filetype:", 9) == 0 || strncmp(buf, "Version:", 8) == 0) continue;

        char *colon = strchr(buf, ':');
        if (colon == NULL) continue;
        *colon = '\0';
        char *key = buf;
        char *value = colon + 1;
        while (*value == ' ') value++;

        if (strcmp(key, "name") == 0) {
            if (in_signal) {
                finalize_signal(&current, proto_name, addr32, cmd32, file);
            }
            memset(&current, 0, sizeof(current));
            strncpy(current.name, value, IR_FILE_NAME_MAX - 1);
            in_signal = true;
            proto_name[0] = '\0';
            addr32 = 0;
            cmd32 = 0;
        }
        else if (strcmp(key, "type") == 0) {
            current.is_raw = (strcmp(value, "raw") == 0);
        }
        else if (strcmp(key, "protocol") == 0) {
            strncpy(proto_name, value, sizeof(proto_name) - 1);
        }
        else if (strcmp(key, "address") == 0) {
            addr32 = parse_hex_bytes(value);
        }
        else if (strcmp(key, "command") == 0) {
            cmd32 = parse_hex_bytes(value);
        }
        else if (strcmp(key, "frequency") == 0) {
            current.frequency = (uint32_t)atol(value);
        }
        else if (strcmp(key, "data") == 0) {
            parse_raw_data(value, &current);
        }
    }

    if (in_signal) {
        finalize_signal(&current, proto_name, addr32, cmd32, file);
    }

    ESP_LOGI(TAG, "Parsed %d signals", file->count);
    return file->count > 0;
}

// ══════════════════════════════════════════════════════
// Generator
// ══════════════════════════════════════════════════════

size_t ir_file_to_string(const ir_file_t *file, char *buf, size_t buf_size) {
    size_t w = 0;

    w += snprintf(buf + w, buf_size - w, "Filetype: IR signals file\nVersion: 1\n");

    for (size_t i = 0; i < file->count; i++) {
        const ir_signal_t *sig = &file->signals[i];
        w += snprintf(buf + w, buf_size - w, "#\nname: %s\n", sig->name);

        if (sig->is_raw) {
            w += snprintf(buf + w, buf_size - w,
                "type: raw\nfrequency: %lu\nduty_cycle: 0.330000\ndata:",
                (unsigned long)sig->frequency);
            for (size_t j = 0; j < sig->raw_count; j++) {
                w += snprintf(buf + w, buf_size - w, " %lu", (unsigned long)sig->raw[j]);
            }
            w += snprintf(buf + w, buf_size - w, "\n");
        } else {
            const char *proto = ir_to_flipper_proto(sig->data.protocol,
                                                     sig->data.address, sig->data.command);
            if (proto != NULL) {
                uint32_t a = sig->data.address;
                uint32_t c = sig->data.command;
                w += snprintf(buf + w, buf_size - w,
                    "type: parsed\n"
                    "protocol: %s\n"
                    "address: %02lX %02lX %02lX %02lX\n"
                    "command: %02lX %02lX %02lX %02lX\n",
                    proto,
                    (unsigned long)(a & 0xFF), (unsigned long)((a >> 8) & 0xFF),
                    (unsigned long)((a >> 16) & 0xFF), (unsigned long)((a >> 24) & 0xFF),
                    (unsigned long)(c & 0xFF), (unsigned long)((c >> 8) & 0xFF),
                    (unsigned long)((c >> 16) & 0xFF), (unsigned long)((c >> 24) & 0xFF));
            } else {
                // Protocol without Flipper equivalent, generate raw
                rmt_symbol_word_t symbols[128];
                size_t count = ir_encode(&sig->data, symbols, 128);
                uint32_t freq = ir_carrier_freq(sig->data.protocol);

                w += snprintf(buf + w, buf_size - w,
                    "type: raw\nfrequency: %lu\nduty_cycle: 0.330000\ndata:",
                    (unsigned long)freq);
                for (size_t j = 0; j < count; j++) {
                    w += snprintf(buf + w, buf_size - w, " %lu %lu",
                        (unsigned long)symbols[j].duration0,
                        (unsigned long)symbols[j].duration1);
                }
                w += snprintf(buf + w, buf_size - w, "\n");
            }
        }
    }

    return w;
}

// ══════════════════════════════════════════════════════
// Find
// ══════════════════════════════════════════════════════

ir_signal_t *ir_file_find(const ir_file_t *file, const char *name) {
    for (size_t i = 0; i < file->count; i++) {
        if (strcmp(file->signals[i].name, name) == 0) {
            return &file->signals[i];
        }
    }
    return NULL;
}

// ══════════════════════════════════════════════════════
// Send
// ══════════════════════════════════════════════════════

void ir_file_send(const ir_signal_t *signal) {
    if (signal->is_raw) {
        // Convert flat timings to RMT symbols
        size_t sym_count = (signal->raw_count + 1) / 2;
        rmt_symbol_word_t *symbols = malloc(sym_count * sizeof(rmt_symbol_word_t));
        if (symbols == NULL) return;

        for (size_t i = 0; i < sym_count; i++) {
            symbols[i].duration0 = signal->raw[i * 2];
            symbols[i].level0 = 1;
            symbols[i].duration1 = (i * 2 + 1 < signal->raw_count) ? signal->raw[i * 2 + 1] : 0;
            symbols[i].level1 = 0;
        }

        ir_send_raw(symbols, sym_count, signal->frequency);
        free(symbols);
    } else {
        ir_send(&signal->data);
    }
}

// ══════════════════════════════════════════════════════
// Add signals
// ══════════════════════════════════════════════════════

void ir_file_add_parsed(ir_file_t *file, const char *name, const ir_data_t *data) {
    ir_file_grow(file);
    ir_signal_t *sig = &file->signals[file->count++];
    memset(sig, 0, sizeof(ir_signal_t));
    strncpy(sig->name, name, IR_FILE_NAME_MAX - 1);
    sig->is_raw = false;
    sig->data = *data;
}

void ir_file_add_raw(ir_file_t *file, const char *name,
                     const rmt_symbol_word_t *symbols, size_t count, uint32_t freq) {
    ir_file_grow(file);
    ir_signal_t *sig = &file->signals[file->count++];
    memset(sig, 0, sizeof(ir_signal_t));
    strncpy(sig->name, name, IR_FILE_NAME_MAX - 1);
    sig->is_raw = true;
    sig->frequency = freq;

    sig->raw_count = count * 2;
    sig->raw = malloc(sig->raw_count * sizeof(uint32_t));
    if (sig->raw == NULL) { sig->raw_count = 0; return; }

    for (size_t i = 0; i < count; i++) {
        sig->raw[i * 2]     = symbols[i].duration0;
        sig->raw[i * 2 + 1] = symbols[i].duration1;
    }

    // Remove trailing zeros
    while (sig->raw_count > 0 && sig->raw[sig->raw_count - 1] == 0) {
        sig->raw_count--;
    }
}
