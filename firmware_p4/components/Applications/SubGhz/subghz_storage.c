#include "subghz_storage.h"
#include "subghz_protocol_serializer.h"
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"

static const char* TAG = "SUBGHZ_STORAGE";

void subghz_storage_init(void) {
    ESP_LOGI(TAG, "Storage API Initialized (Placeholder Mode)");
}

void subghz_storage_save_decoded(const char* name, const subghz_data_t* data, uint32_t frequency, uint32_t te) {
    char* buf = malloc(1024);
    if (!buf) return;

    subghz_protocol_serialize_decoded(data, frequency, te, buf, 1024);

    ESP_LOGI(TAG, "Saving Decoded Signal to %s...", name);
    printf("\n--- FILE CONTENT START (%s) ---\n", name);
    printf("%s", buf);
    printf("--- FILE CONTENT END ---\n\n");

    free(buf);
}

void subghz_storage_save_raw(const char* name, const int32_t* pulses, size_t count, uint32_t frequency) {
    // RAW signals can be large, allocate 4KB
    char* buf = malloc(4096);
    if (!buf) return;

    subghz_protocol_serialize_raw(pulses, count, frequency, buf, 4096);

    ESP_LOGI(TAG, "Saving RAW Signal to %s...", name);
    printf("\n--- FILE CONTENT START (%s) ---\n", name);
    printf("%s", buf);
    printf("--- FILE CONTENT END ---\n\n");

    free(buf);
}
