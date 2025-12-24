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

/**
 * @brief Inicia a Task de Recepção e Configura o Hardware (CC1101 + RMT)
 */
void subghz_receiver_start(void);

/**
 * @brief Para a Task e libera o Hardware
 */
void subghz_receiver_stop(void);

/**
 * @brief Verifica se o receptor está ativo
 */
bool subghz_receiver_is_running(void);

#endif // SUBGHZ_RECEIVER_H
