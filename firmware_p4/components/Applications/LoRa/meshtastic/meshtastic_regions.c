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

#include "meshtastic_regions.h"

#include "esp_log.h"

#include "meshtastic_nvs.h"

static const char *TAG = "MESHTASTIC_REGIONS";

#define MT_REGION_NVS_KEY "region"
#define MT_REGION_FALLBACK_FREQ_HZ 906875000

static const mt_region_info_t REGIONS[] = {
    { MT_REGION_UNSET,   "UNSET",    902000000,   928000000,  100, 30, false },
    { MT_REGION_US,      "US",       902000000,   928000000,  100, 30, false },
    { MT_REGION_EU_433,  "EU_433",   433000000,   434000000,   10, 10, false },
    { MT_REGION_EU_868,  "EU_868",   869400000,   869650000,   10, 27, false },
    { MT_REGION_CN,      "CN",       470000000,   510000000,  100, 19, false },
    { MT_REGION_JP,      "JP",       920500000,   923500000,  100, 13, false },
    { MT_REGION_ANZ,     "ANZ",      915000000,   928000000,  100, 30, false },
    { MT_REGION_KR,      "KR",       920000000,   923000000,  100, 23, false },
    { MT_REGION_TW,      "TW",       920000000,   925000000,  100, 27, false },
    { MT_REGION_RU,      "RU",       868700000,   869200000,  100, 20, false },
    { MT_REGION_IN,      "IN",       865000000,   867000000,  100, 30, false },
    { MT_REGION_NZ_865,  "NZ_865",   864000000,   868000000,  100, 36, false },
    { MT_REGION_TH,      "TH",       920000000,   925000000,   10, 27, false },
    { MT_REGION_LORA_24, "LORA_24", 2400000000U, 2483500000U, 100, 10, true  },
    { MT_REGION_UA_433,  "UA_433",   433000000,   434700000,   10, 10, false },
    { MT_REGION_UA_868,  "UA_868",   868000000,   868600000,    1, 14, false },
    { MT_REGION_MY_433,  "MY_433",   433000000,   435000000,  100, 20, false },
    { MT_REGION_MY_919,  "MY_919",   919000000,   924000000,  100, 27, true  },
    { MT_REGION_SG_923,  "SG_923",   917000000,   925000000,  100, 20, false },
    { MT_REGION_PH_433,  "PH_433",   433000000,   434700000,  100, 10, false },
    { MT_REGION_PH_868,  "PH_868",   868000000,   869400000,  100, 14, false },
    { MT_REGION_PH_915,  "PH_915",   915000000,   918000000,  100, 24, false },
    { MT_REGION_ANZ_433, "ANZ_433",  433050000,   434790000,  100, 14, false },
    { MT_REGION_KZ_433,  "KZ_433",   433075000,   434775000,  100, 10, false },
    { MT_REGION_KZ_863,  "KZ_863",   863000000,   868000000,  100, 30, false },
    { MT_REGION_NP_865,  "NP_865",   865000000,   868000000,  100, 30, false },
    { MT_REGION_BR_902,  "BR_902",   902000000,   907500000,  100, 30, false },
};
#define REGIONS_COUNT (sizeof(REGIONS) / sizeof(REGIONS[0]))

const mt_region_info_t *mt_region_info(mt_region_t id)
{
    for (size_t i = 0; i < REGIONS_COUNT; i++) {
        if (REGIONS[i].id == id) return &REGIONS[i];
    }
    return NULL;
}

int mt_region_count(void)
{
    return (int)REGIONS_COUNT;
}

mt_region_t mt_region_current(void)
{
    uint32_t value = mt_nvs_get_u32(MT_REGION_NVS_KEY, (uint32_t)MT_REGION_US);
    return (mt_region_t)value;
}

void mt_region_set(mt_region_t id)
{
    const mt_region_info_t *info = mt_region_info(id);
    if (info == NULL) {
        ESP_LOGW(TAG, "Region invalid: %d", (int)id);
        return;
    }
    mt_nvs_set_u32(MT_REGION_NVS_KEY, (uint32_t)id);
    ESP_LOGI(TAG, "Region applied: %s (%lu-%lu Hz, duty=%u%%, pwr_max=%ddBm)",
             info->name,
             (unsigned long)info->freq_start_hz,
             (unsigned long)info->freq_stop_hz,
             info->duty_pct, info->tx_pwr_max_dbm);
}

uint32_t mt_region_freq_for_channel(mt_region_t id, uint8_t channel_num, uint32_t bw_hz)
{
    const mt_region_info_t *info = mt_region_info(id);
    if (info == NULL) return MT_REGION_FALLBACK_FREQ_HZ;

    uint32_t span = info->freq_stop_hz - info->freq_start_hz;
    uint32_t num_channels = span / bw_hz;
    if (num_channels == 0) num_channels = 1;
    uint8_t ch = channel_num % num_channels;
    return info->freq_start_hz + (bw_hz / 2) + (ch * bw_hz);
}
