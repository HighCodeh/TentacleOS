/**
 * @file sd_card_info.h
 * @brief Funções para obter informações do cartão SD
 */

#ifndef SD_CARD_INFO_H
#define SD_CARD_INFO_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Informações do cartão SD
 */
typedef struct {
    char name[16];
    uint32_t capacity_mb;
    uint32_t sector_size;
    uint32_t num_sectors;
    uint32_t speed_khz;
    uint8_t card_type;
    bool is_mounted;
} sd_card_info_t;

/**
 * @brief Estatísticas do filesystem
 */
typedef struct {
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t free_bytes;
} sd_fs_stats_t;

/**
 * @brief Obtém informações do cartão
 * @param info Estrutura para receber info
 * @return ESP_OK em sucesso
 */
esp_err_t sd_get_card_info(sd_card_info_t *info);

/**
 * @brief Imprime informações no console
 */
void sd_print_card_info(void);

/**
 * @brief Obtém estatísticas do filesystem
 * @param stats Estrutura para receber stats
 * @return ESP_OK em sucesso
 */
esp_err_t sd_get_fs_stats(sd_fs_stats_t *stats);

/**
 * @brief Obtém espaço livre
 * @param free_bytes Ponteiro para receber bytes livres
 * @return ESP_OK em sucesso
 */
esp_err_t sd_get_free_space(uint64_t *free_bytes);

/**
 * @brief Obtém espaço total
 * @param total_bytes Ponteiro para receber bytes totais
 * @return ESP_OK em sucesso
 */
esp_err_t sd_get_total_space(uint64_t *total_bytes);

/**
 * @brief Obtém espaço usado
 * @param used_bytes Ponteiro para receber bytes usados
 * @return ESP_OK em sucesso
 */
esp_err_t sd_get_used_space(uint64_t *used_bytes);

/**
 * @brief Calcula percentual de uso
 * @param percentage Ponteiro para receber percentual
 * @return ESP_OK em sucesso
 */
esp_err_t sd_get_usage_percent(float *percentage);

/**
 * @brief Obtém nome do cartão
 * @param name Buffer para receber nome
 * @param size Tamanho do buffer
 * @return ESP_OK em sucesso
 */
esp_err_t sd_get_card_name(char *name, size_t size);

/**
 * @brief Obtém capacidade em MB
 * @param capacity_mb Ponteiro para receber capacidade
 * @return ESP_OK em sucesso
 */
esp_err_t sd_get_capacity(uint32_t *capacity_mb);

/**
 * @brief Obtém velocidade do cartão
 * @param speed_khz Ponteiro para receber velocidade
 * @return ESP_OK em sucesso
 */
esp_err_t sd_get_speed(uint32_t *speed_khz);

/**
 * @brief Obtém tipo do cartão
 * @param type Ponteiro para receber tipo
 * @return ESP_OK em sucesso
 */
esp_err_t sd_get_card_type(uint8_t *type);

/**
 * @brief Obtém nome do tipo do cartão
 * @param type_name Buffer para receber nome
 * @param size Tamanho do buffer
 * @return ESP_OK em sucesso
 */
esp_err_t sd_get_card_type_name(char *type_name, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* SD_CARD_INFO_H */