#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t esp_err_t;

#define ESP_OK                   0
#define ESP_FAIL                 (-1)
#define ESP_ERR_NO_MEM           0x101
#define ESP_ERR_INVALID_ARG      0x102
#define ESP_ERR_INVALID_STATE    0x103
#define ESP_ERR_INVALID_SIZE     0x104
#define ESP_ERR_NOT_FOUND        0x105
#define ESP_ERR_NOT_SUPPORTED    0x106
#define ESP_ERR_TIMEOUT          0x107
#define ESP_ERR_INVALID_RESPONSE 0x108
#define ESP_ERR_INVALID_CRC      0x109
#define ESP_ERR_INVALID_VERSION  0x10A
#define ESP_ERR_INVALID_MAC      0x10B
#define ESP_ERR_NOT_FINISHED     0x10C
#define ESP_ERR_NOT_ALLOWED      0x10D

#define ESP_ERR_NVS_BASE              0x1100
#define ESP_ERR_NVS_NOT_INITIALIZED   (ESP_ERR_NVS_BASE + 0x01)
#define ESP_ERR_NVS_NOT_FOUND         (ESP_ERR_NVS_BASE + 0x02)
#define ESP_ERR_NVS_TYPE_MISMATCH     (ESP_ERR_NVS_BASE + 0x03)
#define ESP_ERR_NVS_READ_ONLY         (ESP_ERR_NVS_BASE + 0x04)
#define ESP_ERR_NVS_NOT_ENOUGH_SPACE  (ESP_ERR_NVS_BASE + 0x05)
#define ESP_ERR_NVS_INVALID_NAME      (ESP_ERR_NVS_BASE + 0x06)
#define ESP_ERR_NVS_INVALID_HANDLE    (ESP_ERR_NVS_BASE + 0x07)
#define ESP_ERR_NVS_REMOVE_FAILED     (ESP_ERR_NVS_BASE + 0x08)
#define ESP_ERR_NVS_KEY_TOO_LONG      (ESP_ERR_NVS_BASE + 0x09)
#define ESP_ERR_NVS_INVALID_LENGTH    (ESP_ERR_NVS_BASE + 0x0C)
#define ESP_ERR_NVS_NO_FREE_PAGES     (ESP_ERR_NVS_BASE + 0x0D)
#define ESP_ERR_NVS_VALUE_TOO_LONG    (ESP_ERR_NVS_BASE + 0x0E)
#define ESP_ERR_NVS_NEW_VERSION_FOUND (ESP_ERR_NVS_BASE + 0x10)

#define ESP_OK_CHECK(x) do { esp_err_t _err = (x); if (_err != ESP_OK) return _err; } while(0)

#define ESP_ERROR_CHECK(x) do {                                          \
        esp_err_t _esp_err_rc = (x);                                    \
        if (_esp_err_rc != ESP_OK) {                                    \
            _esp_error_check_failed(_esp_err_rc, __FILE__, __LINE__,    \
                                    __FUNCTION__, #x);                  \
        }                                                               \
    } while(0)

void _esp_error_check_failed(esp_err_t rc, const char *file, int line,
                              const char *func, const char *expr);

#ifdef __cplusplus
}
#endif
