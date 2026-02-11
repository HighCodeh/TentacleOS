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


#include "pn7150.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "PN7150";

// Inicializa I2C mestre do ESP32
void pn7150_i2c_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    i2c_param_config(I2C_MASTER_NUM, &conf);
    i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

// Configura GPIOs VEN (output) e IRQ (input) do PN7150
void pn7150_hw_init(void)
{
    // Configura VEN como saída
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL<<PN7150_PIN_VEN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);
    // Configura IRQ como entrada (sem alterar pull-ups)
    io_conf.pin_bit_mask = (1ULL<<PN7150_PIN_IRQ);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);

    // Reset físico via VEN: low -> delay -> high
    gpio_set_level(PN7150_PIN_VEN, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PN7150_PIN_VEN, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
}

// Envia um comando NCI via I2C
esp_err_t pn7150_send_cmd(const uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    // Inicia e envia endereço+escrita
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (PN7150_I2C_ADDRESS<<1) | I2C_MASTER_WRITE, true);
    // Envia o comando NCI completo
    i2c_master_write(cmd, (uint8_t*)data, len, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    return err;
}

// Lê resposta NCI via I2C. Retorna em buffer e define *length.
esp_err_t pn7150_read_rsp(uint8_t *buffer, size_t *length)
{
    // Leitura de cabeçalho (3 bytes)
    uint8_t header[3] = {0};
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (PN7150_I2C_ADDRESS<<1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, header, 3, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    if (err != ESP_OK) return err;

    // Calcula tamanho do payload
    uint8_t payload_len = header[2];
    *length = 3 + payload_len;
    // Copia cabeçalho para buffer
    buffer[0] = header[0];
    buffer[1] = header[1];
    buffer[2] = header[2];
    // Se não há payload, retorno
    if (payload_len == 0) return ESP_OK;

    // Leitura do payload restante
    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (PN7150_I2C_ADDRESS<<1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, buffer+3, payload_len, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    err = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    return err;
}

// Executa CORE_RESET_CMD (Reset Type = Keep Config = 0x01)
esp_err_t pn7150_core_reset(void)
{
    const uint8_t cmd[] = {0x20, 0x00, 0x01, 0x01};
    ESP_LOGI(TAG, "Enviando CORE_RESET_CMD...");
    esp_err_t err = pn7150_send_cmd(cmd, sizeof(cmd));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro I2C send CORE_RESET");
        return err;
    }
    // Aguarda IRQ ou pode esperar fixo
    //vTaskDelay(pdMS_TO_TICKS(50));
    uint8_t rsp[16] = {0};
    size_t len = 0;
    err = pn7150_read_rsp(rsp, &len);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "CORE_RESET_RSP (len %d):", len);
        for (int i = 0; i < len; i++) {
            printf("%02X ", rsp[i]);
        }
        printf("\n");
    }
    return err;
}

// Executa CORE_INIT_CMD (sem payload)
esp_err_t pn7150_core_init(void)
{
    const uint8_t cmd[] = {0x20, 0x01, 0x00};
    ESP_LOGI(TAG, "Enviando CORE_INIT_CMD...");
    esp_err_t err = pn7150_send_cmd(cmd, sizeof(cmd));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro I2C send CORE_INIT");
        return err;
    }
    // Aguardar IRQ / resposta
    //vTaskDelay(pdMS_TO_TICKS(50));
    uint8_t rsp[32] = {0};
    size_t len = 0;
    err = pn7150_read_rsp(rsp, &len);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "CORE_INIT_RSP (len %d):", len);
        for (int i = 0; i < len; i++) {
            printf("%02X ", rsp[i]);
        }
        printf("\n");
    }
    return err;
}
