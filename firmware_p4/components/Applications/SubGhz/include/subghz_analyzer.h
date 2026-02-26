// Copyright (c) 2025 HIGH CODE LLC
#ifndef SUBGHZ_ANALYZER_H
#define SUBGHZ_ANALYZER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    uint32_t estimated_te;    // Elemento de tempo base em microsegundos
    uint32_t pulse_min;
    uint32_t pulse_max;
    size_t pulse_count;
    const char* modulation_hint; // "PWM", "Manchester" ou "Unknown"
    uint8_t bitstream[128];      // Buffer para bits recuperados
    size_t bitstream_len;        // Quantidade de bits recuperados
} subghz_analyzer_result_t;

/**
 * @brief Analyze a raw signal to extract heuristics
 * @param pulses Array of signed timings
 * @param count Number of pulses
 * @param out_result Pointer to result structure
 * @return true if analysis was successful
 */
bool subghz_analyzer_process(const int32_t* pulses, size_t count, subghz_analyzer_result_t* out_result);

#endif // SUBGHZ_ANALYZER_H
