#include "assets_manager.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h>

static const char *TAG = "ASSETS_MANAGER";

typedef struct asset_node {
    char *path;
    lv_image_dsc_t *dsc;
    bool from_sd;
    struct asset_node *next;
} asset_node_t;

static asset_node_t *assets_head = NULL;

typedef struct __attribute__((packed)) {
    uint32_t magic_cf;
    uint16_t w;
    uint16_t h;
    uint32_t stride;
} bin_header_t;

static lv_image_dsc_t *load_asset_from_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file: %s", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size < sizeof(bin_header_t)) {
        ESP_LOGE(TAG, "File too small to contain header: %s", path);
        fclose(f);
        return NULL;
    }

    bin_header_t header;
    if (fread(&header, 1, sizeof(bin_header_t), f) != sizeof(bin_header_t)) {
        ESP_LOGE(TAG, "Error reading header: %s", path);
        fclose(f);
        return NULL;
    }

    long pixel_data_size = file_size - sizeof(bin_header_t);

    lv_image_dsc_t *dsc = (lv_image_dsc_t *)heap_caps_malloc(sizeof(lv_image_dsc_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *pixel_data = (uint8_t *)heap_caps_malloc(pixel_data_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (!dsc || !pixel_data) {
        ESP_LOGE(TAG, "PSRAM allocation failed for %s. DSC: %p, Data: %p", path, dsc, pixel_data);
        if (dsc) free(dsc);
        if (pixel_data) free(pixel_data);
        fclose(f);
        return NULL;
    }

    if (fread(pixel_data, 1, pixel_data_size, f) != pixel_data_size) {
        ESP_LOGE(TAG, "Error reading pixel data: %s", path);
        free(dsc);
        free(pixel_data);
        fclose(f);
        return NULL;
    }

    fclose(f);

    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf = LV_COLOR_FORMAT_ARGB8888;
    dsc->header.w = header.w;
    dsc->header.h = header.h;
    dsc->header.stride = header.stride;
    dsc->header.flags = 0;
    dsc->data_size = pixel_data_size;
    dsc->data = pixel_data;

    ESP_LOGI(TAG, "Loaded: %s (%dx%d)", path, header.w, header.h);
    return dsc;
}

static void add_asset_to_list(const char *path, lv_image_dsc_t *dsc, bool from_sd) {
    asset_node_t *node = malloc(sizeof(asset_node_t));
    if (!node) {
        ESP_LOGE(TAG, "Error allocating list node for %s", path);
        return;
    }
    node->path = strdup(path);
    node->dsc = dsc;
    node->from_sd = from_sd;
    node->next = assets_head;
    assets_head = node;
}

static void scan_and_load_recursive(const char *base_path) {
    DIR *dir = opendir(base_path);
    if (!dir) return;

    struct dirent *ent;
    char path[512];

    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_type == DT_DIR) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            snprintf(path, sizeof(path), "%s/%s", base_path, ent->d_name);
            scan_and_load_recursive(path);
        } else if (ent->d_type == DT_REG) {
            size_t len = strlen(ent->d_name);
            if (len > 4 && strcmp(ent->d_name + len - 4, ".bin") == 0) {
                snprintf(path, sizeof(path), "%s/%s", base_path, ent->d_name);
                
                if (assets_get(path) == NULL) {
                    lv_image_dsc_t *dsc = load_asset_from_file(path);
                    if (dsc) {
                        add_asset_to_list(path, dsc, false);
                    }
                }
            }
        }
    }
    closedir(dir);
}

void assets_manager_init(void) {
    ESP_LOGI(TAG, "Starting assets loading...");
    
    struct stat st;
    if (stat("/assets", &st) == 0) {
        scan_and_load_recursive("/assets");
    } else {
        ESP_LOGE(TAG, "Directory /assets not found!");
    }
    
    ESP_LOGI(TAG, "Assets loading finished.");
}

lv_image_dsc_t * assets_get(const char * path) {
    asset_node_t *curr = assets_head;
    while (curr) {
        if (strcmp(curr->path, path) == 0) {
            return curr->dsc;
        }
        curr = curr->next;
    }
    return NULL;
}

void assets_manager_free_all(void) {
    asset_node_t *curr = assets_head;
    while (curr) {
        asset_node_t *next = curr->next;
        if (curr->dsc) {
            if (curr->dsc->data) free((void*)curr->dsc->data);
            free(curr->dsc);
        }
        if (curr->path) free(curr->path);
        free(curr);
        curr = next;
    }
    assets_head = NULL;
}

static void free_node_dsc(asset_node_t *node) {
    if (node->dsc) {
        if (node->dsc->data) free((void *)node->dsc->data);
        free(node->dsc);
        node->dsc = NULL;
    }
}

static bool replace_asset_in_list(const char *key, lv_image_dsc_t *dsc) {
    asset_node_t *curr = assets_head;
    while (curr) {
        if (strcmp(curr->path, key) == 0) {
            free_node_dsc(curr);
            curr->dsc = dsc;
            curr->from_sd = true;
            return true;
        }
        curr = curr->next;
    }
    return false;
}

int assets_load_from_sd(const char *sd_dir, const char *flash_prefix) {
    if (!sd_dir || !flash_prefix) return 0;

    DIR *dir = opendir(sd_dir);
    if (!dir) {


        return 0;
    }

    int count = 0;
    struct dirent *ent;
    char sd_path[512];
    char cache_key[512];

    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_type != DT_REG) continue;

        size_t len = strlen(ent->d_name);
        if (len <= 4 || strcmp(ent->d_name + len - 4, ".bin") != 0) continue;

        snprintf(sd_path, sizeof(sd_path), "%s/%s", sd_dir, ent->d_name);
        snprintf(cache_key, sizeof(cache_key), "%s/%s", flash_prefix, ent->d_name);

        lv_image_dsc_t *dsc = load_asset_from_file(sd_path);
        if (!dsc) continue;

        if (!replace_asset_in_list(cache_key, dsc)) {


            
            add_asset_to_list(cache_key, dsc, true);
        }

        ESP_LOGI(TAG, "SD override: %s -> %s", sd_path, cache_key);
        count++;
    }

    closedir(dir);
    ESP_LOGI(TAG, "Loaded %d asset(s) from SD dir: %s", count, sd_dir);
    return count;
}

void assets_unload_sd(void) {
    asset_node_t **pp = &assets_head;
    int removed = 0;

    while (*pp) {
        asset_node_t *node = *pp;
        if (node->from_sd) {
            *pp = node->next;
            free_node_dsc(node);
            if (node->path) free(node->path);
            free(node);
            removed++;
        } else {
            pp = &node->next;
        }
    }

    ESP_LOGI(TAG, "Unloaded %d SD asset(s) from cache", removed);
}