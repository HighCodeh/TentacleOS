// Copyright (c) 2025 HIGH CODE LLC
#ifndef SUBGHZ_PROTOCOL_DEFS_H
#define SUBGHZ_PROTOCOL_DEFS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Estrutura genérica para dados decodificados
typedef struct {
    const char* protocol_name;
    uint32_t serial;
    uint8_t btn;
    uint8_t bit_count;
    uint32_t raw_value; // Valor bruto se couber em 32/64 bits
} subghz_data_t;

// Interface do Protocolo
typedef struct {
    const char* name;
    
    // Tenta decodificar o sinal RAW. Retorna true se sucesso.
    // raw_data: array de durações (positivos=high, negativos=low)
    // count: numero de itens no array
    // out_data: ponteiro para estrutura onde salvar o resultado
    bool (*decode)(const int32_t* raw_data, size_t count, subghz_data_t* out_data);

    // Codifica dados para RAW (para transmissão)
    // in_data: dados para enviar
    // out_raw: ponteiro para array int32_t (alocado internamente, caller deve dar free)
    // out_count: tamanho do array gerado
    void (*encode)(const subghz_data_t* in_data, int32_t** out_raw, size_t* out_count);

} subghz_protocol_t;

#endif // SUBGHZ_PROTOCOL_DEFS_H
