#ifndef SUBGHZ_RECEIVER_H
#define SUBGHZ_RECEIVER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Estrutura para representar um sinal RAW capturado
// Basicamente uma lista de durações (em microsegundos)
// Valores positivos = High (Pulse), Negativos = Low (Gap/Space)
typedef struct {
    int32_t *items;
    size_t count;
} subghz_raw_signal_t;

typedef enum {
    SUBGHZ_MODE_SCAN, // Tenta decodificar protocolos conhecidos
    SUBGHZ_MODE_RAW   // Exibe apenas o sinal bruto (Sniffer/Analyser)
} subghz_mode_t;

typedef enum {
    SUBGHZ_MODULATION_ASK, // Amplitude Shift Keying (OOK) - Padrão
    SUBGHZ_MODULATION_FSK  // Frequency Shift Keying (Intelbras, etc)
} subghz_modulation_t;

/**
 * @brief Inicia a Task de Recepção e Configura o Hardware (CC1101 + RMT)
 * @param mode Define se o receptor vai decodificar (SCAN) ou apenas mostrar RAW.
 * @param mod Modulação (ASK ou FSK).
 * @param freq Frequência em Hz (ex: 433920000).
 */
void subghz_receiver_start(subghz_mode_t mode, subghz_modulation_t mod, uint32_t freq);

/**
 * @brief Para a Task e libera o Hardware
 */
void subghz_receiver_stop(void);

/**
 * @brief Verifica se o receptor está ativo
 */
bool subghz_receiver_is_running(void);

#endif // SUBGHZ_RECEIVER_H
