// Host-test shim for esp_log.h. The frozen reference file includes this header;
// it makes no logging calls, so the macros are no-ops.
#ifndef SHIM_ESP_LOG_H
#define SHIM_ESP_LOG_H

#define ESP_LOGE(tag, ...) ((void)0)
#define ESP_LOGW(tag, ...) ((void)0)
#define ESP_LOGI(tag, ...) ((void)0)
#define ESP_LOGD(tag, ...) ((void)0)
#define ESP_LOGV(tag, ...) ((void)0)

#endif // SHIM_ESP_LOG_H
