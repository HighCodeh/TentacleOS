/**
 * @file sd_card_read.h
 * @brief Funções para leitura de dados do SD
 */
#ifndef SD_CARD_READ_H
#define SD_CARD_READ_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback para processar linhas
 */
typedef void (*sd_line_callback_t)(const char *line, void *user_data);

/**
 * @brief Lê arquivo como string
 * @param path Caminho do arquivo
 * @param buffer Buffer para receber dados
 * @param buffer_size Tamanho do buffer
 * @return ESP_OK em sucesso
 */
esp_err_t sd_read_string(const char *path, char *buffer, size_t buffer_size);

/**
 * @brief Lê dados binários
 * @param path Caminho do arquivo
 * @param buffer Buffer para receber dados
 * @param size Tamanho a ler
 * @param bytes_read Ponteiro para receber bytes lidos
 * @return ESP_OK em sucesso
 */
esp_err_t sd_read_binary(const char *path, void *buffer, size_t size, size_t *bytes_read);

/**
 * @brief Lê uma linha específica
 * @param path Caminho do arquivo
 * @param buffer Buffer para receber linha
 * @param buffer_size Tamanho do buffer
 * @param line_number Número da linha (1-based)
 * @return ESP_OK em sucesso
 */
esp_err_t sd_read_line(const char *path, char *buffer, size_t buffer_size, uint32_t line_number);

/**
 * @brief Lê todas as linhas via callback
 * @param path Caminho do arquivo
 * @param callback Função callback
 * @param user_data Dados do usuário
 * @return ESP_OK em sucesso
 */
esp_err_t sd_read_lines(const char *path, sd_line_callback_t callback, void *user_data);

/**
 * @brief Conta número de linhas
 * @param path Caminho do arquivo
 * @param line_count Ponteiro para receber contagem
 * @return ESP_OK em sucesso
 */
esp_err_t sd_count_lines(const char *path, uint32_t *line_count);

/**
 * @brief Lê chunk de dados
 * @param path Caminho do arquivo
 * @param offset Offset inicial
 * @param buffer Buffer para receber dados
 * @param size Tamanho a ler
 * @param bytes_read Ponteiro para receber bytes lidos
 * @return ESP_OK em sucesso
 */
esp_err_t sd_read_chunk(const char *path, size_t offset, void *buffer, size_t size, size_t *bytes_read);

/**
 * @brief Lê primeira linha
 * @param path Caminho do arquivo
 * @param buffer Buffer para receber linha
 * @param buffer_size Tamanho do buffer
 * @return ESP_OK em sucesso
 */
esp_err_t sd_read_first_line(const char *path, char *buffer, size_t buffer_size);

/**
 * @brief Lê última linha
 * @param path Caminho do arquivo
 * @param buffer Buffer para receber linha
 * @param buffer_size Tamanho do buffer
 * @return ESP_OK em sucesso
 */
esp_err_t sd_read_last_line(const char *path, char *buffer, size_t buffer_size);

/**
 * @brief Lê inteiro
 * @param path Caminho do arquivo
 * @param value Ponteiro para receber valor
 * @return ESP_OK em sucesso
 */
esp_err_t sd_read_int(const char *path, int32_t *value);

/**
 * @brief Lê float
 * @param path Caminho do arquivo
 * @param value Ponteiro para receber valor
 * @return ESP_OK em sucesso
 */
esp_err_t sd_read_float(const char *path, float *value);

/**
 * @brief Lê bytes
 * @param path Caminho do arquivo
 * @param bytes Buffer para bytes
 * @param max_count Máximo de bytes
 * @param count Ponteiro para receber contagem
 * @return ESP_OK em sucesso
 */
esp_err_t sd_read_bytes(const char *path, uint8_t *bytes, size_t max_count, size_t *count);

/**
 * @brief Lê um byte
 * @param path Caminho do arquivo
 * @param byte Ponteiro para receber byte
 * @return ESP_OK em sucesso
 */
esp_err_t sd_read_byte(const char *path, uint8_t *byte);

/**
 * @brief Verifica se arquivo contém string
 * @param path Caminho do arquivo
 * @param search String de busca
 * @param found Ponteiro para resultado
 * @return ESP_OK em sucesso
 */
esp_err_t sd_file_contains(const char *path, const char *search, bool *found);

/**
 * @brief Conta ocorrências de string
 * @param path Caminho do arquivo
 * @param search String de busca
 * @param count Ponteiro para receber contagem
 * @return ESP_OK em sucesso
 */
esp_err_t sd_count_occurrences(const char *path, const char *search, uint32_t *count);

#ifdef __cplusplus
}
#endif

#endif /* SD_CARD_READ_H */