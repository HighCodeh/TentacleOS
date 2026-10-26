// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#include "firmware_crypto.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#define AES_TAG_MAX_SIZE 16U

static const char *TAG = "FIRMWARE_CRYPTO";

static bool valid_span(firmware_crypto_span_t span);
static esp_err_t result(psa_status_t status);
static psa_status_t import_aes(const firmware_crypto_cipher_t *params, psa_key_id_t *out_key);

void firmware_crypto_wipe(void *data, size_t size) {
  volatile uint8_t *bytes = data;
  while (size-- != 0)
    *bytes++ = 0;
}

esp_err_t firmware_crypto_sha256(firmware_crypto_span_t input, uint8_t out_data[32]) {
  if (!valid_span(input) || out_data == NULL)
    return ESP_ERR_INVALID_ARG;
  size_t written = 0;
  psa_status_t status = psa_hash_compute(PSA_ALG_SHA_256, input.data, input.size,
                                       out_data, FIRMWARE_CRYPTO_SHA256_SIZE, &written);
  if (status != PSA_SUCCESS || written != FIRMWARE_CRYPTO_SHA256_SIZE) {
    firmware_crypto_wipe(out_data, FIRMWARE_CRYPTO_SHA256_SIZE);
    return result(status == PSA_SUCCESS ? PSA_ERROR_GENERIC_ERROR : status);
  }
  return ESP_OK;
}

esp_err_t firmware_crypto_mac(firmware_crypto_span_t key, firmware_crypto_span_t input,
                             psa_algorithm_t algorithm, uint8_t *out_data) {
  if (!valid_span(key) || !valid_span(input) || out_data == NULL ||
      (algorithm != PSA_ALG_CMAC && algorithm != PSA_ALG_HMAC(PSA_ALG_SHA_256)))
    return ESP_ERR_INVALID_ARG;
  psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
  psa_key_id_t key_id = 0;
  size_t size = algorithm == PSA_ALG_CMAC ? FIRMWARE_CRYPTO_AES_BLOCK_SIZE : FIRMWARE_CRYPTO_SHA256_SIZE;
  size_t written = 0;
  psa_set_key_type(&attributes, algorithm == PSA_ALG_CMAC ? PSA_KEY_TYPE_AES : PSA_KEY_TYPE_HMAC);
  psa_set_key_algorithm(&attributes, algorithm);
  psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
  psa_status_t status = psa_import_key(&attributes, key.data, key.size, &key_id);
  if (status == PSA_SUCCESS)
    status = psa_mac_compute(key_id, algorithm, input.data, input.size, out_data, size, &written);
  psa_status_t cleanup_status = psa_destroy_key(key_id);
  psa_reset_key_attributes(&attributes);
  if (status == PSA_SUCCESS)
    status = cleanup_status;
  if (status != PSA_SUCCESS || written != size) {
    firmware_crypto_wipe(out_data, size);
    return result(status == PSA_SUCCESS ? PSA_ERROR_GENERIC_ERROR : status);
  }
  return ESP_OK;
}

esp_err_t firmware_crypto_hkdf(const firmware_crypto_hkdf_t *params, uint8_t *out_data, size_t size) {
  if (params == NULL || !valid_span(params->secret) || !valid_span(params->salt) ||
      !valid_span(params->info) || out_data == NULL)
    return ESP_ERR_INVALID_ARG;
  psa_key_derivation_operation_t operation = PSA_KEY_DERIVATION_OPERATION_INIT;
  psa_algorithm_t algorithm = params->is_expand_only ? PSA_ALG_HKDF_EXPAND(PSA_ALG_SHA_256)
                                                    : PSA_ALG_HKDF(PSA_ALG_SHA_256);
  psa_status_t status = psa_key_derivation_setup(&operation, algorithm);
  if (status == PSA_SUCCESS && !params->is_expand_only)
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SALT,
                                           params->salt.data, params->salt.size);
  if (status == PSA_SUCCESS)
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SECRET,
                                           params->secret.data, params->secret.size);
  if (status == PSA_SUCCESS)
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_INFO,
                                           params->info.data, params->info.size);
  if (status == PSA_SUCCESS)
    status = psa_key_derivation_output_bytes(&operation, out_data, size);
  psa_status_t cleanup_status = psa_key_derivation_abort(&operation);
  if (status == PSA_SUCCESS)
    status = cleanup_status;
  if (status != PSA_SUCCESS)
    firmware_crypto_wipe(out_data, size);
  return result(status);
}

esp_err_t firmware_crypto_cipher(const firmware_crypto_cipher_t *params,
                                firmware_crypto_span_t input, uint8_t *out_data) {
  if (params == NULL || !valid_span(params->key) || !valid_span(params->iv) ||
      !valid_span(input) || out_data == NULL || input.size > SIZE_MAX - FIRMWARE_CRYPTO_AES_BLOCK_SIZE)
    return ESP_ERR_INVALID_ARG;
  psa_key_id_t key_id = 0;
  psa_cipher_operation_t operation = PSA_CIPHER_OPERATION_INIT;
  size_t capacity = input.size + FIRMWARE_CRYPTO_AES_BLOCK_SIZE;
  size_t written = 0, final_size = 0;
  psa_status_t status = PSA_ERROR_INSUFFICIENT_MEMORY;
  uint8_t *buffer = malloc(capacity);
  if (buffer == NULL) {
    ESP_LOGE(TAG, "Cannot allocate cipher output");
    goto cleanup;
  }
  status = import_aes(params, &key_id);
  if (status == PSA_SUCCESS)
    status = params->is_encrypt ? psa_cipher_encrypt_setup(&operation, key_id, params->algorithm)
                               : psa_cipher_decrypt_setup(&operation, key_id, params->algorithm);
  if (status == PSA_SUCCESS && params->iv.size != 0)
    status = psa_cipher_set_iv(&operation, params->iv.data, params->iv.size);
  if (status == PSA_SUCCESS)
    status = psa_cipher_update(&operation, input.data, input.size, buffer, capacity, &written);
  if (status == PSA_SUCCESS)
    status = psa_cipher_finish(&operation, buffer + written, capacity - written, &final_size);
  if (status == PSA_SUCCESS && written + final_size != input.size)
    status = PSA_ERROR_GENERIC_ERROR;
cleanup:
  {
    psa_status_t abort_status = psa_cipher_abort(&operation);
    psa_status_t destroy_status = psa_destroy_key(key_id);
    if (status == PSA_SUCCESS)
      status = abort_status != PSA_SUCCESS ? abort_status : destroy_status;
  }
  if (status == PSA_SUCCESS)
    memcpy(out_data, buffer, input.size);
  else
    firmware_crypto_wipe(out_data, input.size);
  if (buffer != NULL)
    firmware_crypto_wipe(buffer, capacity);
  free(buffer);
  return result(status);
}

esp_err_t firmware_crypto_aead(const firmware_crypto_aead_t *params,
                              firmware_crypto_span_t input, uint8_t *out_data) {
  if (params == NULL || !valid_span(params->cipher.key) || !valid_span(params->cipher.iv) ||
      !valid_span(params->aad) || !valid_span(input) || out_data == NULL || params->tag == NULL ||
      params->tag_size == 0 || params->tag_size > AES_TAG_MAX_SIZE ||
      input.size > SIZE_MAX - params->tag_size)
    return ESP_ERR_INVALID_ARG;
  size_t capacity = input.size + params->tag_size;
  size_t written = 0;
  psa_key_id_t key_id = 0;
  psa_status_t status = PSA_ERROR_INSUFFICIENT_MEMORY;
  uint8_t *buffer = malloc(capacity);
  if (buffer == NULL) {
    ESP_LOGE(TAG, "Cannot allocate authenticated cipher output");
    goto cleanup;
  }
  status = import_aes(&params->cipher, &key_id);
  if (status != PSA_SUCCESS)
    goto cleanup;
  if (params->cipher.is_encrypt) {
    status = psa_aead_encrypt(key_id, params->cipher.algorithm,
                             params->cipher.iv.data, params->cipher.iv.size,
                             params->aad.data, params->aad.size, input.data, input.size,
                             buffer, capacity, &written);
    if (status == PSA_SUCCESS && written != capacity)
      status = PSA_ERROR_GENERIC_ERROR;
  } else {
    memcpy(buffer, input.data, input.size);
    memcpy(buffer + input.size, params->tag, params->tag_size);
    status = psa_aead_decrypt(key_id, params->cipher.algorithm,
                             params->cipher.iv.data, params->cipher.iv.size,
                             params->aad.data, params->aad.size, buffer, capacity,
                             buffer, capacity, &written);
    if (status == PSA_SUCCESS && written != input.size)
      status = PSA_ERROR_GENERIC_ERROR;
  }
cleanup:
  {
    psa_status_t destroy_status = psa_destroy_key(key_id);
    if (status == PSA_SUCCESS)
      status = destroy_status;
  }
  if (status == PSA_SUCCESS) {
    memcpy(out_data, buffer, input.size);
    if (params->cipher.is_encrypt)
      memcpy(params->tag, buffer + input.size, params->tag_size);
  } else {
    firmware_crypto_wipe(out_data, input.size);
    if (params->cipher.is_encrypt)
      firmware_crypto_wipe(params->tag, params->tag_size);
  }
  if (buffer != NULL)
    firmware_crypto_wipe(buffer, capacity);
  free(buffer);
  return result(status);
}

esp_err_t firmware_crypto_x25519(const uint8_t private_key[32], const uint8_t peer[32],
                                uint8_t out_data[32]) {
  if (private_key == NULL || peer == NULL || out_data == NULL)
    return ESP_ERR_INVALID_ARG;
  psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
  psa_key_id_t key_id = 0;
  size_t written = 0;
  psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_MONTGOMERY));
  psa_set_key_bits(&attributes, FIRMWARE_CRYPTO_X25519_BITS);
  psa_set_key_algorithm(&attributes, PSA_ALG_ECDH);
  psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE);
  psa_status_t status = psa_import_key(&attributes, private_key, FIRMWARE_CRYPTO_X25519_SIZE, &key_id);
  if (status == PSA_SUCCESS)
    status = psa_raw_key_agreement(PSA_ALG_ECDH, key_id, peer, FIRMWARE_CRYPTO_X25519_SIZE,
                                   out_data, FIRMWARE_CRYPTO_X25519_SIZE, &written);
  psa_status_t cleanup_status = psa_destroy_key(key_id);
  psa_reset_key_attributes(&attributes);
  if (status == PSA_SUCCESS)
    status = cleanup_status;
  if (status != PSA_SUCCESS || written != FIRMWARE_CRYPTO_X25519_SIZE) {
    firmware_crypto_wipe(out_data, FIRMWARE_CRYPTO_X25519_SIZE);
    return result(status == PSA_SUCCESS ? PSA_ERROR_GENERIC_ERROR : status);
  }
  return ESP_OK;
}

esp_err_t firmware_crypto_x25519_generate(uint8_t out_private_key[32], uint8_t out_public_key[32]) {
  if (out_private_key == NULL || out_public_key == NULL)
    return ESP_ERR_INVALID_ARG;
  psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
  psa_key_id_t key_id = 0;
  size_t written = 0;
  psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_MONTGOMERY));
  psa_set_key_bits(&attributes, FIRMWARE_CRYPTO_X25519_BITS);
  psa_set_key_algorithm(&attributes, PSA_ALG_ECDH);
  psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE | PSA_KEY_USAGE_EXPORT);
  psa_status_t status = psa_generate_key(&attributes, &key_id);
  if (status == PSA_SUCCESS)
    status = psa_export_key(key_id, out_private_key, FIRMWARE_CRYPTO_X25519_SIZE, &written);
  if (status == PSA_SUCCESS && written != FIRMWARE_CRYPTO_X25519_SIZE)
    status = PSA_ERROR_GENERIC_ERROR;
  if (status == PSA_SUCCESS)
    status = psa_export_public_key(key_id, out_public_key, FIRMWARE_CRYPTO_X25519_SIZE, &written);
  psa_status_t cleanup_status = psa_destroy_key(key_id);
  psa_reset_key_attributes(&attributes);
  if (status == PSA_SUCCESS)
    status = cleanup_status;
  if (status != PSA_SUCCESS || written != FIRMWARE_CRYPTO_X25519_SIZE) {
    firmware_crypto_wipe(out_private_key, FIRMWARE_CRYPTO_X25519_SIZE);
    firmware_crypto_wipe(out_public_key, FIRMWARE_CRYPTO_X25519_SIZE);
    return result(status == PSA_SUCCESS ? PSA_ERROR_GENERIC_ERROR : status);
  }
  return ESP_OK;
}

static bool valid_span(firmware_crypto_span_t span) {
  return span.data != NULL || span.size == 0;
}

static esp_err_t result(psa_status_t status) {
  if (status == PSA_SUCCESS)
    return ESP_OK;
  ESP_LOGW(TAG, "PSA operation failed: %ld", (long)status);
  return status == PSA_ERROR_INSUFFICIENT_MEMORY ? ESP_ERR_NO_MEM : ESP_FAIL;
}

static psa_status_t import_aes(const firmware_crypto_cipher_t *params, psa_key_id_t *out_key) {
  psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
  psa_set_key_algorithm(&attributes, params->algorithm);
  psa_set_key_usage_flags(&attributes, params->is_encrypt ? PSA_KEY_USAGE_ENCRYPT : PSA_KEY_USAGE_DECRYPT);
  psa_status_t status = psa_import_key(&attributes, params->key.data, params->key.size, out_key);
  psa_reset_key_attributes(&attributes);
  return status;
}

