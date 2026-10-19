/* Real OpenSSL HKDF/HMAC + real libsodium X25519; explicit NVS/RTOS/RNG fakes. */
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>
#include "esp_err.h"
#include "mbedtls/md.h"
#include "nvs.h"
#include "host_link_sec.h"

static pthread_mutex_t s_lock;
static int64_t s_now=1;
static uint8_t s_rng=1;
static uint8_t s_psk[32];
static int s_persist_error;
static int s_mac_error;
void *xSemaphoreCreateRecursiveMutex(void) {
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&s_lock,&attr);pthread_mutexattr_destroy(&attr);return &s_lock;
}
int xSemaphoreTakeRecursive(void *lock,uint32_t timeout) {(void)timeout;return pthread_mutex_lock(lock)==0;}
int xSemaphoreGiveRecursive(void *lock) {return pthread_mutex_unlock(lock)==0;}
const char *esp_err_to_name(esp_err_t error) {(void)error;return "fixture";}
int64_t esp_timer_get_time(void) {return s_now;}
void esp_fill_random(void *buffer,size_t length) {uint8_t *out=buffer;for(size_t i=0;i<length;++i)out[i]=s_rng++;}
esp_err_t esp_read_mac(uint8_t *out,int type) {(void)type;memcpy(out,"ABCDEF",6);return s_mac_error;}
void host_link_reset_rx(void) {host_link_sec_reset();}
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *out) {(void)name;(void)mode;*out=1;return ESP_OK;}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *length) {
 (void)h;(void)key;assert(*length==sizeof(s_psk));memcpy(out,s_psk,sizeof(s_psk));return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t size) {
 (void)h;(void)key;assert(size==sizeof(s_psk));if(!s_persist_error)memcpy(s_psk,data,size);return s_persist_error;
}
esp_err_t nvs_commit(nvs_handle_t h) {(void)h;return s_persist_error;}
void nvs_close(nvs_handle_t h) {(void)h;}
const mbedtls_md_info_t *mbedtls_md_info_from_type(int type) {static mbedtls_md_info_t info;assert(type==1);return &info;}
int mbedtls_md_hmac(const mbedtls_md_info_t *info,const uint8_t *key,size_t key_length,
                   const uint8_t *input,size_t length,uint8_t *out) {
 assert(info);unsigned size=0;return HMAC(EVP_sha256(),key,(int)key_length,input,length,out,&size)&&size==32?0:-1;
}
int mbedtls_hkdf(const mbedtls_md_info_t *info,const uint8_t *salt,size_t salt_length,
                const uint8_t *key,size_t key_length,const uint8_t *label,size_t label_length,
                uint8_t *out,size_t length) {
 assert(info);EVP_PKEY_CTX *context=EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF,NULL);if(!context)return -1;
 int ok=EVP_PKEY_derive_init(context)>0 && EVP_PKEY_CTX_set_hkdf_md(context,EVP_sha256())>0 &&
   EVP_PKEY_CTX_set1_hkdf_salt(context,salt,(int)salt_length)>0 &&
   EVP_PKEY_CTX_set1_hkdf_key(context,key,(int)key_length)>0 &&
   EVP_PKEY_CTX_add1_hkdf_info(context,label,(int)label_length)>0 &&
   EVP_PKEY_derive(context,out,&length)>0;
 EVP_PKEY_CTX_free(context);return ok?0:-1;
}
void mbedtls_platform_zeroize(void *buffer,size_t length) {volatile uint8_t *p=buffer;while(length--)*p++=0;}
void pairing_test_clock(int64_t now) {s_now=now;}
void pairing_test_fail_mac(int error) {s_mac_error=error;}
void pairing_test_fail_persist(int error) {s_persist_error=error;}
void pairing_test_boot(void) {for(unsigned i=0;i<sizeof(s_psk);++i)s_psk[i]=(uint8_t)(i+1);assert(host_link_sec_init()==ESP_OK);}
