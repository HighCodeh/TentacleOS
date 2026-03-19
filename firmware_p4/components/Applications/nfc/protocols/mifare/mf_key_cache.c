/**
 * @file mf_key_cache.c
 * @brief MIFARE UID-keyed sector key cache — in-memory + NVS-backed persistence.
 */

#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "highboy_nfc_error.h"
#include "mf_key_cache.h"

static const char* TAG = "mf_key_cache";

#define NVS_NS          "nfc_kcache"
#define NVS_KEY_COUNT   "count"
#define NVS_ENTRY_PFX   "e_"

/* -------------------------------------------------------------------------
 * In-memory state
 * ------------------------------------------------------------------------- */
static mf_key_cache_entry_t s_cache[MF_KEY_CACHE_MAX_CARDS];
static int                  s_count = 0;

/* -------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

static int find_entry(const uint8_t* uid, uint8_t uid_len)
{
    for (int i = 0; i < s_count; i++) {
        if (s_cache[i].uid_len == uid_len &&
            memcmp(s_cache[i].uid, uid, uid_len) == 0) {
            return i;
        }
    }
    return -1;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void mf_key_cache_init(void)
{
    s_count = 0;
    memset(s_cache, 0, sizeof(s_cache));

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed (0x%x), starting with empty cache", err);
        return;
    }

    uint8_t count = 0;
    err = nvs_get_u8(handle, NVS_KEY_COUNT, &count);
    if (err != ESP_OK || count == 0) {
        nvs_close(handle);
        return;
    }

    if (count > MF_KEY_CACHE_MAX_CARDS) {
        count = MF_KEY_CACHE_MAX_CARDS;
    }

    for (uint8_t i = 0; i < count; i++) {
        char entry_name[16];
        snprintf(entry_name, sizeof(entry_name), NVS_ENTRY_PFX "%u", (unsigned)i);

        size_t len = sizeof(mf_key_cache_entry_t);
        err = nvs_get_blob(handle, entry_name, &s_cache[s_count], &len);
        if (err == ESP_OK && len == sizeof(mf_key_cache_entry_t)) {
            s_count++;
        } else {
            ESP_LOGW(TAG, "Failed to load cache entry %s (err 0x%x)", entry_name, err);
        }
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "Loaded %d cache entries from NVS", s_count);
}

bool mf_key_cache_lookup(const uint8_t* uid, uint8_t uid_len,
                          int sector, mf_key_type_t type,
                          uint8_t key_out[6])
{
    if (!uid || sector < 0 || sector >= MF_KEY_CACHE_MAX_SECTORS) {
        return false;
    }

    int idx = find_entry(uid, uid_len);
    if (idx < 0) {
        return false;
    }

    const mf_key_cache_entry_t* entry = &s_cache[idx];

    if (type == MF_KEY_A) {
        if (!entry->key_a_known[sector]) {
            return false;
        }
        memcpy(key_out, entry->key_a[sector], 6);
        return true;
    } else {
        if (!entry->key_b_known[sector]) {
            return false;
        }
        memcpy(key_out, entry->key_b[sector], 6);
        return true;
    }
}

void mf_key_cache_store(const uint8_t* uid, uint8_t uid_len,
                         int sector, int sector_count,
                         mf_key_type_t type,
                         const uint8_t key[6])
{
    if (!uid || !key || sector < 0 || sector >= MF_KEY_CACHE_MAX_SECTORS) {
        return;
    }

    int idx = find_entry(uid, uid_len);

    if (idx < 0) {
        if (s_count >= MF_KEY_CACHE_MAX_CARDS) {
            /* Evict oldest entry (index 0), shift everything down. */
            ESP_LOGW(TAG, "Cache full, evicting oldest entry");
            memmove(&s_cache[0], &s_cache[1],
                    (MF_KEY_CACHE_MAX_CARDS - 1) * sizeof(mf_key_cache_entry_t));
            s_count--;
        }

        idx = s_count;
        memset(&s_cache[idx], 0, sizeof(mf_key_cache_entry_t));
        memcpy(s_cache[idx].uid, uid, uid_len);
        s_cache[idx].uid_len = uid_len;
        s_cache[idx].sector_count = sector_count;
        s_count++;
    } else if (sector_count > s_cache[idx].sector_count) {
        s_cache[idx].sector_count = sector_count;
    }

    if (type == MF_KEY_A) {
        memcpy(s_cache[idx].key_a[sector], key, 6);
        s_cache[idx].key_a_known[sector] = true;
    } else {
        memcpy(s_cache[idx].key_b[sector], key, 6);
        s_cache[idx].key_b_known[sector] = true;
    }
}

void mf_key_cache_save(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open for save failed (0x%x)", err);
        return;
    }

    for (int i = 0; i < s_count; i++) {
        char entry_name[16];
        snprintf(entry_name, sizeof(entry_name), NVS_ENTRY_PFX "%d", i);

        err = nvs_set_blob(handle, entry_name, &s_cache[i],
                           sizeof(mf_key_cache_entry_t));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "NVS set_blob for entry %d failed (0x%x)", i, err);
        }
    }

    err = nvs_set_u8(handle, NVS_KEY_COUNT, (uint8_t)s_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS set count failed (0x%x)", err);
    }

    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Saved %d cache entries to NVS", s_count);
}

void mf_key_cache_clear_uid(const uint8_t* uid, uint8_t uid_len)
{
    if (!uid) {
        return;
    }

    int idx = find_entry(uid, uid_len);
    if (idx < 0) {
        return;
    }

    /* Shift remaining entries down to fill the gap. */
    if (idx < s_count - 1) {
        memmove(&s_cache[idx], &s_cache[idx + 1],
                (s_count - idx - 1) * sizeof(mf_key_cache_entry_t));
    }
    memset(&s_cache[s_count - 1], 0, sizeof(mf_key_cache_entry_t));
    s_count--;

    /* Persist the updated cache to NVS. */
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open for clear failed (0x%x)", err);
        return;
    }

    /* Rewrite all live entries and update count. Stale entries beyond
     * the new count are ignored on next load (count guards the loop). */
    for (int i = 0; i < s_count; i++) {
        char entry_name[16];
        snprintf(entry_name, sizeof(entry_name), NVS_ENTRY_PFX "%d", i);
        err = nvs_set_blob(handle, entry_name, &s_cache[i],
                           sizeof(mf_key_cache_entry_t));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "NVS set_blob during clear rewrite failed (0x%x)", err);
        }
    }

    err = nvs_set_u8(handle, NVS_KEY_COUNT, (uint8_t)s_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS set count after clear failed (0x%x)", err);
    }

    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Cleared UID entry from cache (remaining: %d)", s_count);
}

int mf_key_cache_get_known_count(const uint8_t* uid, uint8_t uid_len)
{
    if (!uid) {
        return 0;
    }

    int idx = find_entry(uid, uid_len);
    if (idx < 0) {
        return 0;
    }

    const mf_key_cache_entry_t* entry = &s_cache[idx];
    int known = 0;
    int sectors = entry->sector_count;
    if (sectors > MF_KEY_CACHE_MAX_SECTORS) {
        sectors = MF_KEY_CACHE_MAX_SECTORS;
    }

    for (int s = 0; s < sectors; s++) {
        if (entry->key_a_known[s]) known++;
        if (entry->key_b_known[s]) known++;
    }

    return known;
}
