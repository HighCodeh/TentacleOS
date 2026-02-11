#include "buzzer.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "storage_assets.h"
#include "esp_log.h"
#include <sys/stat.h>
#include <string.h>

static const char *TAG = "BUZZER_PLAYER";

#define SOUND_PATH_PREFIX "/assets/config/buzzer/sounds/"
#define SOUND_EXTENSION ".conf"
#define CONFIG_PATH "/assets/config/buzzer/buzzer.conf"

static int g_buzzer_volume = 3;
static bool g_buzzer_enabled = true;

void buzzer_save_config(void) {
    if (!storage_assets_is_mounted()) return;

    mkdir("/assets/config", 0777);
    mkdir("/assets/config/buzzer", 0777);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "volume", g_buzzer_volume);
    cJSON_AddNumberToObject(root, "enabled", g_buzzer_enabled ? 1 : 0);
    
    char *out = cJSON_PrintUnformatted(root);
    if (out) {
        FILE *f = fopen(CONFIG_PATH, "w");
        if (f) {
            fputs(out, f);
            fclose(f);
            ESP_LOGI(TAG, "Config do buzzer salva.");
        } else {
            ESP_LOGE(TAG, "Erro ao abrir %s para escrita", CONFIG_PATH);
        }
        cJSON_free(out);
    }
    cJSON_Delete(root);
}

void buzzer_load_config(void) {
    if (!storage_assets_is_mounted()) return;

    FILE *f = fopen(CONFIG_PATH, "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);

        char *data = malloc(fsize + 1);
        if (data) {
            fread(data, 1, fsize, f);
            data[fsize] = 0;
            cJSON *root = cJSON_Parse(data);
            if (root) {
                cJSON *vol = cJSON_GetObjectItem(root, "volume");
                cJSON *en = cJSON_GetObjectItem(root, "enabled");
                if (cJSON_IsNumber(vol)) g_buzzer_volume = vol->valueint;
                if (cJSON_IsNumber(en)) g_buzzer_enabled = (en->valueint == 1);
                cJSON_Delete(root);
            }
            free(data);
        }
        fclose(f);
    } else {
        ESP_LOGW(TAG, "Arquivo de config do buzzer não encontrado, usando padrões.");
    }
}

void buzzer_set_volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 5) volume = 5;
    g_buzzer_volume = volume;
    buzzer_save_config();
}

void buzzer_set_enabled(bool enabled) {
    g_buzzer_enabled = enabled;
    buzzer_save_config();
}

int buzzer_get_volume(void) {
    return g_buzzer_volume;
}

bool buzzer_is_enabled(void) {
    return g_buzzer_enabled;
}

esp_err_t buzzer_init(void) {
    buzzer_load_config();
    
    ledc_timer_config_t timer_conf = {
        .duty_resolution = LEDC_DUTY_RESOLUTION,
        .freq_hz = LEDC_FREQ,
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .clk_cfg = LEDC_AUTO_CLK
    };
    esp_err_t err = ledc_timer_config(&timer_conf);
    if (err != ESP_OK) return err;

    ledc_channel_config_t ch_conf = {
        .channel    = LEDC_CHANNEL,
        .duty       = 0,
        .gpio_num   = BUZZER_GPIO,
        .speed_mode = LEDC_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_TIMER
    };
    return ledc_channel_config(&ch_conf);
}

void buzzer_play_tone(uint32_t freq_hz, uint32_t duration_ms) {
    if (g_buzzer_enabled && g_buzzer_volume > 0 && freq_hz > 0) {
        uint32_t max_duty_val = (1 << LEDC_DUTY_RESOLUTION) - 1;

        uint32_t duty = 0;
        switch (g_buzzer_volume) {
            case 5: duty = max_duty_val / 2;
                break;
            case 4: duty = max_duty_val / 8;
                break;
            case 3: duty = max_duty_val / 32;
                break;
            case 2: duty = max_duty_val / 128; 
                break;
            case 1: duty = max_duty_val / 512;
                break;
            default: duty = 0;
        }
        
        ledc_set_freq(LEDC_MODE, LEDC_TIMER, freq_hz);
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    }
    
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

esp_err_t buzzer_play_sound_file(const char *sound_name) {
    if (sound_name == NULL) return ESP_ERR_INVALID_ARG;

    char full_path[128];
    snprintf(full_path, sizeof(full_path), "%s%s%s", SOUND_PATH_PREFIX, sound_name, SOUND_EXTENSION);

    FILE *f = fopen(full_path, "r");
    if (f == NULL) return ESP_ERR_NOT_FOUND;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *json_data = malloc(fsize + 1);
    if (json_data) {
        fread(json_data, 1, fsize, f);
        json_data[fsize] = 0;
        fclose(f);

        cJSON *root = cJSON_Parse(json_data);
        if (root) {
            cJSON *notes = cJSON_GetObjectItem(root, "notes");
            if (cJSON_IsArray(notes)) {
                int array_size = cJSON_GetArraySize(notes);
                for (int i = 0; i < array_size; i++) {
                    cJSON *item = cJSON_GetArrayItem(notes, i);
                    uint32_t freq = cJSON_GetObjectItem(item, "freq")->valueint;
                    uint32_t duration = cJSON_GetObjectItem(item, "duration")->valueint;
                    buzzer_play_tone(freq, duration);
                }
            }
            cJSON_Delete(root);
        }
        free(json_data);
    } else {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}