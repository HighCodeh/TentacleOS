// Copyright (c) 2025 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file lf_rfid.h
 * @brief Native High Boy V2 125 kHz RFID antenna/demodulator driver.
 *
 * Hardware revision notes:
 * - T2/T4/T5 are Si2302CDS N-channel MOSFETs, so GPIO2 and GPIO11 are
 *   active-high in this driver.
 * - T3 (AO3401A) has PCB pads 1 and 2 swapped in the user's revised assembly.
 *   Firmware cannot compensate for a physical gate/source wiring error: run
 *   lf_rfid_self_test() with a current-limited supply before continuous field.
 * - LF and NFC share the antenna and are interlocked by rf_frontend.
 */

#ifndef LF_RFID_H
#define LF_RFID_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define LF_RFID_NOMINAL_HZ            125000U
#define LF_RFID_MIN_HZ                110000U
#define LF_RFID_MAX_HZ                140000U
#define LF_RFID_DEFAULT_DUTY_PERMILLE 500U
#define LF_RFID_RAW_MAX_EDGES         512U

typedef struct {
  uint32_t delta_us;
  uint8_t level;
} lf_rfid_edge_t;

typedef struct {
  uint32_t requested_hz;
  uint32_t generated_hz;
  uint32_t measured_hz;
  int32_t frequency_error_ppm;
  uint32_t period_min_ns;
  uint32_t period_max_ns;
  uint32_t period_jitter_ppm;
  uint16_t carrier_duty_permille;
  uint32_t carrier_edges;
  uint32_t data_edges;
  uint32_t data_edge_rate_hz;
  uint32_t window_ms;
  uint8_t quality_percent;
  bool carrier_present;
  bool field_active;
} lf_rfid_field_report_t;

typedef struct {
  bool gpio_static_ok;
  bool carrier_present;
  bool frequency_ok;
  lf_rfid_field_report_t field;
} lf_rfid_self_test_t;

typedef struct {
  uint8_t bytes[5];
  uint64_t id;
  bool inverted;
  uint16_t samples_used;
} lf_rfid_em4100_t;

esp_err_t lf_rfid_init(void);
void lf_rfid_deinit(void);

esp_err_t lf_rfid_field_start(uint32_t frequency_hz, uint16_t duty_permille);
void lf_rfid_field_stop(void);
bool lf_rfid_field_is_on(void);
uint32_t lf_rfid_field_frequency(void);

/** Measure digital carrier feedback and demodulator activity. Field must be on.
 */
esp_err_t lf_rfid_measure(uint32_t window_ms, lf_rfid_field_report_t *out);

/**
 * Listen for an externally generated 125 kHz field with both LF output MOSFET
 * controls held low. This is intended for Proxmark-as-reader bench tests.
 */
esp_err_t lf_rfid_listen_external(uint32_t window_ms, lf_rfid_field_report_t *out);

/**
 * Briefly toggle the GPIO11/T2 load path while an external 125 kHz field is
 * present. This validates the hardware modulation path; it does not emit a
 * protocol-valid RFID frame. The active High Boy field always remains off.
 */
esp_err_t lf_rfid_modulation_test(uint32_t rate_hz,
                                  uint32_t duration_ms,
                                  lf_rfid_field_report_t *external_field);

/** Capture transition intervals from 125KHz_DA while the LF field is active. */
esp_err_t lf_rfid_capture_edges(uint32_t duration_ms,
                                lf_rfid_edge_t *edges,
                                size_t capacity,
                                size_t *out_count);

/** Read and parity-check a 64-bit Manchester EM4100/EM4102 frame. */
esp_err_t lf_rfid_read_em4100(uint32_t timeout_ms, lf_rfid_em4100_t *out);

/** Short, current-conscious bring-up test; the field is always off on return.
 */
esp_err_t lf_rfid_self_test(lf_rfid_self_test_t *out);

#ifdef __cplusplus
}
#endif

#endif // LF_RFID_H
