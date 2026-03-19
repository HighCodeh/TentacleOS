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
/*
 * Serialization format (binary blob):
 *  [0] uid_len (1 byte)
 *  [1..10] uid (10 bytes, padded)
 *  [11..12] atqa (2 bytes)
 *  [13] sak (1 byte)
 *  [14] type (1 byte)
 *  [15..16] sector_count (2 bytes LE)
 *  [17..18] total_blocks (2 bytes LE)
 *  [19..] keys: for each sector (sector_count):
 *  key_a[6] + key_b[6] + key_a_known(1) + key_b_known(1) = 14 bytes
 *  [...] blocks: total_blocks 16 bytes
 *
 * Total for MFC 1K: 19 + 16*14 + 64*16 = 19 + 224 + 1024 = 1267 bytes
 * Total for MFC 4K: 19 + 40*14 + 256*16 = 19 + 560 + 4096 = 4675 bytes
 */
#include "nfc_device.h"
#include "nfc_store.h"
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "mf_key_dict.h"
#include "mf_key_cache.h"

static const char* TAG = "nfc_dev";

#define SERIALIZE_HEADER_SIZE   19
#define SERIALIZE_KEY_SIZE      14
#define SERIALIZE_MAX_SIZE      (SERIALIZE_HEADER_SIZE + \
                                 MFC_EMU_MAX_SECTORS * SERIALIZE_KEY_SIZE + \
                                 MFC_EMU_MAX_BLOCKS * MFC_EMU_BLOCK_SIZE)

static bool s_initialized = false;

static size_t serialize_card(const mfc_emu_card_data_t* card, uint8_t* buf, size_t buf_max)
{
    size_t pos = 0;

    if (buf_max < SERIALIZE_HEADER_SIZE) return 0;

    buf[pos++] = card->uid_len;
    memcpy(&buf[pos], card->uid, 10); pos += 10;
    memcpy(&buf[pos], card->atqa, 2); pos += 2;
    buf[pos++] = card->sak;
    buf[pos++] = (uint8_t)card->type;
    buf[pos++] = (uint8_t)(card->sector_count & 0xFF);
    buf[pos++] = (uint8_t)((card->sector_count >> 8) & 0xFF);
    buf[pos++] = (uint8_t)(card->total_blocks & 0xFF);
    buf[pos++] = (uint8_t)((card->total_blocks >> 8) & 0xFF);

    for (int s = 0; s < card->sector_count && s < MFC_EMU_MAX_SECTORS; s++) {
        if (pos + SERIALIZE_KEY_SIZE > buf_max) return 0;
        memcpy(&buf[pos], card->keys[s].key_a, 6); pos += 6;
        memcpy(&buf[pos], card->keys[s].key_b, 6); pos += 6;
        buf[pos++] = card->keys[s].key_a_known ? 1 : 0;
        buf[pos++] = card->keys[s].key_b_known ? 1 : 0;
    }

    for (int b = 0; b < card->total_blocks && b < MFC_EMU_MAX_BLOCKS; b++) {
        if (pos + MFC_EMU_BLOCK_SIZE > buf_max) return 0;
        memcpy(&buf[pos], card->blocks[b], MFC_EMU_BLOCK_SIZE);
        pos += MFC_EMU_BLOCK_SIZE;
    }

    return pos;
}

static bool deserialize_card(const uint8_t* buf, size_t len, mfc_emu_card_data_t* card)
{
    if (len < SERIALIZE_HEADER_SIZE) return false;

    memset(card, 0, sizeof(*card));
    size_t pos = 0;

    card->uid_len = buf[pos++];
    if (card->uid_len > 10) return false;
    memcpy(card->uid, &buf[pos], 10); pos += 10;
    memcpy(card->atqa, &buf[pos], 2); pos += 2;
    card->sak = buf[pos++];
    card->type = (mf_classic_type_t)buf[pos++];
    card->sector_count = buf[pos] | (buf[pos+1] << 8); pos += 2;
    card->total_blocks = buf[pos] | (buf[pos+1] << 8); pos += 2;

    if (card->sector_count > MFC_EMU_MAX_SECTORS) return false;
    if (card->total_blocks > MFC_EMU_MAX_BLOCKS) return false;

    for (int s = 0; s < card->sector_count; s++) {
        if (pos + SERIALIZE_KEY_SIZE > len) return false;
        memcpy(card->keys[s].key_a, &buf[pos], 6); pos += 6;
        memcpy(card->keys[s].key_b, &buf[pos], 6); pos += 6;
        card->keys[s].key_a_known = (buf[pos++] != 0);
        card->keys[s].key_b_known = (buf[pos++] != 0);
    }

    for (int b = 0; b < card->total_blocks; b++) {
        if (pos + MFC_EMU_BLOCK_SIZE > len) return false;
        memcpy(card->blocks[b], &buf[pos], MFC_EMU_BLOCK_SIZE);
        pos += MFC_EMU_BLOCK_SIZE;
    }

    return true;
}

static nvs_handle_t open_nvs(nvs_open_mode_t mode)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NFC_DEVICE_NVS_NAMESPACE, mode, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return 0;
    }
    return handle;
}

hb_nfc_err_t nfc_device_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS full or version mismatch erasing...");
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
        return HB_NFC_ERR_INTERNAL;
    }

    mf_key_dict_init();
    mf_key_cache_init();
    nfc_store_init();

    s_initialized = true;
    ESP_LOGI(TAG, "NFC device storage initialized (%d profiles max)",
             NFC_DEVICE_MAX_PROFILES);
    return HB_NFC_OK;
}

hb_nfc_err_t nfc_device_save(const char* name, const mfc_emu_card_data_t* card)
{
    if (!s_initialized || !name || !card) return HB_NFC_ERR_PARAM;

    nvs_handle_t nvs = open_nvs(NVS_READWRITE);
    if (!nvs) return HB_NFC_ERR_INTERNAL;

    uint8_t count = 0;
    nvs_get_u8(nvs, "count", &count);

    int slot = -1;
    for (int i = 0; i < count && i < NFC_DEVICE_MAX_PROFILES; i++) {
        char key[16];
        snprintf(key, sizeof(key), "name_%d", i);
        char existing[NFC_DEVICE_NAME_MAX_LEN] = { 0 };
        size_t len = sizeof(existing);
        if (nvs_get_str(nvs, key, existing, &len) == ESP_OK) {
            if (strcmp(existing, name) == 0) {
                slot = i;
                break;
            }
        }
    }

    if (slot < 0) {
        if (count >= NFC_DEVICE_MAX_PROFILES) {
            ESP_LOGW(TAG, "Max profiles reached (%d)", NFC_DEVICE_MAX_PROFILES);
            nvs_close(nvs);
            return HB_NFC_ERR_INTERNAL;
        }
        slot = count;
        count++;
    }

    static uint8_t buf[SERIALIZE_MAX_SIZE];
    size_t data_len = serialize_card(card, buf, sizeof(buf));
    if (data_len == 0) {
        nvs_close(nvs);
        return HB_NFC_ERR_INTERNAL;
    }

    char key[16];
    snprintf(key, sizeof(key), "name_%d", slot);
    esp_err_t err = nvs_set_str(nvs, key, name);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS set name failed: %s", esp_err_to_name(err));
        nvs_close(nvs);
        return HB_NFC_ERR_INTERNAL;
    }

    snprintf(key, sizeof(key), "data_%d", slot);
    err = nvs_set_blob(nvs, key, buf, data_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS set data failed: %s", esp_err_to_name(err));
        nvs_close(nvs);
        return HB_NFC_ERR_INTERNAL;
    }

    nvs_set_u8(nvs, "count", count);

    err = nvs_commit(nvs);
    nvs_close(nvs);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS commit failed: %s", esp_err_to_name(err));
        return HB_NFC_ERR_INTERNAL;
    }

    ESP_LOGI(TAG, "Profile '%s' saved (slot %d, %zu bytes)", name, slot, data_len);
    return HB_NFC_OK;
}

hb_nfc_err_t nfc_device_load(int index, mfc_emu_card_data_t* card)
{
    if (!s_initialized || !card) return HB_NFC_ERR_PARAM;

    nvs_handle_t nvs = open_nvs(NVS_READONLY);
    if (!nvs) return HB_NFC_ERR_INTERNAL;

    uint8_t count = 0;
    nvs_get_u8(nvs, "count", &count);
    if (index < 0 || index >= count) {
        nvs_close(nvs);
        return HB_NFC_ERR_PARAM;
    }

    char key[16];
    snprintf(key, sizeof(key), "data_%d", index);

    static uint8_t buf[SERIALIZE_MAX_SIZE];
    size_t len = sizeof(buf);
    esp_err_t err = nvs_get_blob(nvs, key, buf, &len);
    nvs_close(nvs);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS load failed: %s", esp_err_to_name(err));
        return HB_NFC_ERR_INTERNAL;
    }

    if (!deserialize_card(buf, len, card)) {
        ESP_LOGE(TAG, "Deserialization failed");
        return HB_NFC_ERR_INTERNAL;
    }

    ESP_LOGI(TAG, "Profile %d loaded (%zu bytes, UID=%02X%02X%02X%02X)",
             index, len, card->uid[0], card->uid[1], card->uid[2], card->uid[3]);
    return HB_NFC_OK;
}

hb_nfc_err_t nfc_device_load_by_name(const char* name, mfc_emu_card_data_t* card)
{
    if (!s_initialized || !name || !card) return HB_NFC_ERR_PARAM;

    nvs_handle_t nvs = open_nvs(NVS_READONLY);
    if (!nvs) return HB_NFC_ERR_INTERNAL;

    uint8_t count = 0;
    nvs_get_u8(nvs, "count", &count);

    for (int i = 0; i < count && i < NFC_DEVICE_MAX_PROFILES; i++) {
        char key[16];
        snprintf(key, sizeof(key), "name_%d", i);
        char existing[NFC_DEVICE_NAME_MAX_LEN] = { 0 };
        size_t len = sizeof(existing);
        if (nvs_get_str(nvs, key, existing, &len) == ESP_OK) {
            if (strcmp(existing, name) == 0) {
                nvs_close(nvs);
                return nfc_device_load(i, card);
            }
        }
    }

    nvs_close(nvs);
    return HB_NFC_ERR_PARAM;
}

hb_nfc_err_t nfc_device_delete(int index)
{
    if (!s_initialized) return HB_NFC_ERR_PARAM;

    nvs_handle_t nvs = open_nvs(NVS_READWRITE);
    if (!nvs) return HB_NFC_ERR_INTERNAL;

    uint8_t count = 0;
    nvs_get_u8(nvs, "count", &count);
    if (index < 0 || index >= count) {
        nvs_close(nvs);
        return HB_NFC_ERR_PARAM;
    }

    for (int i = index; i < count - 1; i++) {
        char src_key[16], dst_key[16];

        snprintf(src_key, sizeof(src_key), "name_%d", i + 1);
        snprintf(dst_key, sizeof(dst_key), "name_%d", i);
        char name_buf[NFC_DEVICE_NAME_MAX_LEN] = { 0 };
        size_t name_len = sizeof(name_buf);
        if (nvs_get_str(nvs, src_key, name_buf, &name_len) == ESP_OK) {
            nvs_set_str(nvs, dst_key, name_buf);
        }

        snprintf(src_key, sizeof(src_key), "data_%d", i + 1);
        snprintf(dst_key, sizeof(dst_key), "data_%d", i);
        static uint8_t blob[SERIALIZE_MAX_SIZE];
        size_t blob_len = sizeof(blob);
        if (nvs_get_blob(nvs, src_key, blob, &blob_len) == ESP_OK) {
            nvs_set_blob(nvs, dst_key, blob, blob_len);
        }
    }

    char key[16];
    snprintf(key, sizeof(key), "name_%d", count - 1);
    nvs_erase_key(nvs, key);
    snprintf(key, sizeof(key), "data_%d", count - 1);
    nvs_erase_key(nvs, key);

    count--;
    nvs_set_u8(nvs, "count", count);
    nvs_commit(nvs);
    nvs_close(nvs);

    ESP_LOGI(TAG, "Profile %d deleted, %d remaining", index, count);
    return HB_NFC_OK;
}

int nfc_device_get_count(void)
{
    if (!s_initialized) return 0;

    nvs_handle_t nvs = open_nvs(NVS_READONLY);
    if (!nvs) return 0;

    uint8_t count = 0;
    nvs_get_u8(nvs, "count", &count);
    nvs_close(nvs);
    return (int)count;
}

hb_nfc_err_t nfc_device_get_info(int index, nfc_device_profile_info_t* info)
{
    if (!s_initialized || !info) return HB_NFC_ERR_PARAM;

    nvs_handle_t nvs = open_nvs(NVS_READONLY);
    if (!nvs) return HB_NFC_ERR_INTERNAL;

    uint8_t count = 0;
    nvs_get_u8(nvs, "count", &count);
    if (index < 0 || index >= count) {
        nvs_close(nvs);
        return HB_NFC_ERR_PARAM;
    }

    memset(info, 0, sizeof(*info));

    char key[16];
    snprintf(key, sizeof(key), "name_%d", index);
    size_t name_len = sizeof(info->name);
    nvs_get_str(nvs, key, info->name, &name_len);

    snprintf(key, sizeof(key), "data_%d", index);
    uint8_t header[SERIALIZE_HEADER_SIZE];
    size_t len = sizeof(header);
    esp_err_t err = nvs_get_blob(nvs, key, header, &len);
    nvs_close(nvs);

    if (err != ESP_OK || len < SERIALIZE_HEADER_SIZE) {
        return HB_NFC_ERR_INTERNAL;
    }

    info->uid_len = header[0];
    memcpy(info->uid, &header[1], 10);
    info->sak = header[13];
    info->type = (mf_classic_type_t)header[14];
    info->sector_count = header[15] | (header[16] << 8);

    mfc_emu_card_data_t card;
    if (nfc_device_load(index, &card) == HB_NFC_OK) {
        info->keys_known = 0;
        info->complete = true;
        for (int s = 0; s < card.sector_count; s++) {
            if (card.keys[s].key_a_known) info->keys_known++;
            if (card.keys[s].key_b_known) info->keys_known++;
            if (!card.keys[s].key_a_known && !card.keys[s].key_b_known) {
                info->complete = false;
            }
        }
    }

    return HB_NFC_OK;
}

int nfc_device_list(nfc_device_profile_info_t* infos, int max_count)
{
    int count = nfc_device_get_count();
    if (count > max_count) count = max_count;

    for (int i = 0; i < count; i++) {
        nfc_device_get_info(i, &infos[i]);
    }
    return count;
}

hb_nfc_err_t nfc_device_set_active(int index)
{
    if (!s_initialized) return HB_NFC_ERR_PARAM;

    nvs_handle_t nvs = open_nvs(NVS_READWRITE);
    if (!nvs) return HB_NFC_ERR_INTERNAL;

    nvs_set_u8(nvs, "active", (uint8_t)index);
    nvs_commit(nvs);
    nvs_close(nvs);
    return HB_NFC_OK;
}

int nfc_device_get_active(void)
{
    if (!s_initialized) return -1;

    nvs_handle_t nvs = open_nvs(NVS_READONLY);
    if (!nvs) return -1;

    uint8_t active = 0xFF;
    nvs_get_u8(nvs, "active", &active);
    nvs_close(nvs);

    return (active == 0xFF) ? -1 : (int)active;
}

hb_nfc_err_t nfc_device_save_generic(const char* name, const hb_nfc_card_data_t* card)
{
    (void)name; (void)card;
    return HB_NFC_ERR_INTERNAL; /* deferred - nfc_store not yet implemented */
}

hb_nfc_err_t nfc_device_load_generic(const char* name, hb_nfc_card_data_t* card)
{
    (void)name; (void)card;
    return HB_NFC_ERR_NOT_FOUND; /* deferred - nfc_store not yet implemented */
}

const char* nfc_device_protocol_name(hb_nfc_protocol_t proto)
{
    switch (proto) {
    case HB_PROTO_ISO14443_3A:   return "ISO14443-3A";
    case HB_PROTO_ISO14443_3B:   return "ISO14443-3B";
    case HB_PROTO_ISO14443_4A:   return "ISO14443-4A (ISO-DEP)";
    case HB_PROTO_ISO14443_4B:   return "ISO14443-4B";
    case HB_PROTO_FELICA:        return "FeliCa";
    case HB_PROTO_ISO15693:      return "ISO15693 (NFC-V)";
    case HB_PROTO_ST25TB:        return "ST25TB";
    case HB_PROTO_MF_CLASSIC:    return "MIFARE Classic";
    case HB_PROTO_MF_ULTRALIGHT: return "MIFARE Ultralight/NTAG";
    case HB_PROTO_MF_DESFIRE:    return "MIFARE DESFire";
    case HB_PROTO_MF_PLUS:       return "MIFARE Plus";
    case HB_PROTO_SLIX:          return "SLIX";
    default:                     return "Unknown";
    }
}
