#include "subghz_file_loader.h"
#include "subghz_protocol_registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "storage_assets.h"

static const char *TAG = "SUBGHZ_LOADER";

#define MAX_LINE_LEN 2048
#define INITIAL_BUF_SIZE 2048 // Start with 2048 samples

static void process_file(const char *filepath) {
    ESP_LOGI(TAG, "Processing file: %s", filepath);

    FILE *f = fopen(filepath, "r");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        return;
    }

    int32_t *raw_data = malloc(INITIAL_BUF_SIZE * sizeof(int32_t));
    if (!raw_data) {
        ESP_LOGE(TAG, "OOM: Failed to allocate initial buffer");
        fclose(f);
        return;
    }

    size_t capacity = INITIAL_BUF_SIZE;
    size_t count = 0;
    char *line = malloc(MAX_LINE_LEN);
    if (!line) {
         ESP_LOGE(TAG, "OOM: Failed to allocate line buffer");
         free(raw_data);
         fclose(f);
         return;
    }

    while (fgets(line, MAX_LINE_LEN, f)) {
        if (strncmp(line, "RAW_Data:", 9) == 0) {
            char *ptr = line + 9;
            char *endptr;
            
            while (*ptr) {
                // Skip whitespace
                while (*ptr == ' ' || *ptr == '\t') ptr++;
                if (*ptr == '\0' || *ptr == '\r' || *ptr == '\n') break;

                long val = strtol(ptr, &endptr, 10);
                if (ptr == endptr) break; // Conversion failed or no number
                ptr = endptr;

                // Add to buffer
                if (count >= capacity) {
                    size_t new_cap = capacity * 2;
                    int32_t *new_buf = realloc(raw_data, new_cap * sizeof(int32_t));
                    if (!new_buf) {
                        ESP_LOGE(TAG, "OOM: Failed to grow buffer to %d", new_cap);
                        break;
                    }
                    raw_data = new_buf;
                    capacity = new_cap;
                }
                raw_data[count++] = (int32_t)val;
            }
        }
    }

    free(line);
    fclose(f);

    if (count > 0) {
        ESP_LOGI(TAG, "Loaded %d samples from %s. Decoding...", count, filepath);
        subghz_data_t result = {0};
        
        // Ensure result is clean
        memset(&result, 0, sizeof(result));

        if (subghz_process_raw(raw_data, count, &result)) {
            ESP_LOGI(TAG, ">>> SUCCESS [%s] Protocol: %s | Serial: 0x%lX | Btn: 0x%X | Bits: %d", 
                     filepath, result.protocol_name, result.serial, result.btn, result.bit_count);
        } else {
            ESP_LOGW(TAG, ">>> FAILED [%s] No protocol identified", filepath);
        }
    } else {
        ESP_LOGW(TAG, "No RAW_Data found in file: %s", filepath);
    }

    free(raw_data);
}

void subghz_loader_process_directory(const char* dir_path) {
    if (!storage_assets_is_mounted()) {
        ESP_LOGE(TAG, "Assets partition not mounted! Call storage_assets_init() first.");
        return;
    }

    ESP_LOGI(TAG, "Scanning directory: %s", dir_path);

    DIR *dir = opendir(dir_path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory: %s", dir_path);
        return;
    }

    struct dirent *entry;
    int files_processed = 0;

    while ((entry = readdir(dir)) != NULL) {
        // Skip hidden files or current/parent
        if (entry->d_name[0] == '.') continue;

        char fullpath[256];
        int len = snprintf(fullpath, sizeof(fullpath), "%s/%s", dir_path, entry->d_name);
        if (len >= sizeof(fullpath)) continue;

        struct stat st;
        if (stat(fullpath, &st) == 0 && S_ISREG(st.st_mode)) {
            // Check extension .sub
            char *ext = strrchr(entry->d_name, '.');
            if (ext && strcmp(ext, ".sub") == 0) {
                process_file(fullpath);
                files_processed++;
            }
        }
    }
    closedir(dir);
    
    ESP_LOGI(TAG, "Done. Processed %d files.", files_processed);
}
