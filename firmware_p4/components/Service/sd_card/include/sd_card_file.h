/**
 * @file sd_card_file.h
 * @brief Operações de gerenciamento de arquivos
 */

#ifndef SD_CARD_FILE_H
#define SD_CARD_FILE_H

#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Informações de arquivo
 */
typedef struct {
    char path[256];
    size_t size;
    time_t modified_time;
    bool is_directory;
} sd_file_info_t;

/**
 * @brief Verifica se arquivo existe
 * @param path Caminho do arquivo
 * @return true se existe
 */
bool sd_file_exists(const char *path);

/**
 * @brief Deleta arquivo
 * @param path Caminho do arquivo
 * @return ESP_OK em sucesso
 */
esp_err_t sd_file_delete(const char *path);

/**
 * @brief Renomeia ou move arquivo
 * @param old_path Caminho antigo
 * @param new_path Caminho novo
 * @return ESP_OK em sucesso
 */
esp_err_t sd_file_rename(const char *old_path, const char *new_path);

/**
 * @brief Copia arquivo
 * @param src_path Origem
 * @param dst_path Destino
 * @return ESP_OK em sucesso
 */
esp_err_t sd_file_copy(const char *src_path, const char *dst_path);

/**
 * @brief Obtém tamanho do arquivo
 * @param path Caminho do arquivo
 * @param size Ponteiro para receber tamanho
 * @return ESP_OK em sucesso
 */
esp_err_t sd_file_get_size(const char *path, size_t *size);

/**
 * @brief Obtém informações do arquivo
 * @param path Caminho do arquivo
 * @param info Estrutura para receber info
 * @return ESP_OK em sucesso
 */
esp_err_t sd_file_get_info(const char *path, sd_file_info_t *info);

/**
 * @brief Trunca arquivo para tamanho específico
 * @param path Caminho do arquivo
 * @param size Novo tamanho
 * @return ESP_OK em sucesso
 */
esp_err_t sd_file_truncate(const char *path, size_t size);

/**
 * @brief Verifica se arquivo está vazio
 * @param path Caminho do arquivo
 * @param is_empty Ponteiro para receber resultado
 * @return ESP_OK em sucesso
 */
esp_err_t sd_file_is_empty(const char *path, bool *is_empty);

/**
 * @brief Move arquivo
 * @param src_path Origem
 * @param dst_path Destino
 * @return ESP_OK em sucesso
 */
esp_err_t sd_file_move(const char *src_path, const char *dst_path);

/**
 * @brief Compara dois arquivos
 * @param path1 Caminho arquivo 1
 * @param path2 Caminho arquivo 2
 * @param are_equal Ponteiro para resultado
 * @return ESP_OK em sucesso
 */
esp_err_t sd_file_compare(const char *path1, const char *path2, bool *are_equal);

/**
 * @brief Limpa conteúdo do arquivo (torna vazio)
 * @param path Caminho do arquivo
 * @return ESP_OK em sucesso
 */
esp_err_t sd_file_clear(const char *path);

/**
 * @brief Obtém extensão do arquivo
 * @param path Caminho do arquivo
 * @param extension Buffer para receber extensão
 * @param size Tamanho do buffer
 * @return ESP_OK em sucesso
 */
esp_err_t sd_file_get_extension(const char *path, char *extension, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* SD_CARD_FILE_H */