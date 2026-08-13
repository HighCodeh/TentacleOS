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

#include "meshtastic_tcp.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sys_prio.h"
#include "lwip/sockets.h"
#include "mdns.h"

#include "meshtastic_transport.h"

static const char *TAG = "MESH_TCP";

#define TCP_PORT                  4403
#define TCP_TASK_STACK            4096
#define TCP_TASK_PRIO SYS_PRIO_SERVICE_HI
#define TCP_TASK_TICK_MS          50
#define TCP_RX_BUF_SIZE           512
#define TCP_FRAME_MAX             512
#define TCP_HEADER_LEN            4
#define TCP_FRAME_START1          0x94
#define TCP_FRAME_START2          0xC3
#define TCP_SEND_MUTEX_TIMEOUT_MS 100
#define TCP_HOSTNAME_LEN          32
#define TCP_INSTANCE_LEN          32

static volatile bool s_is_running = false;
static int s_server_fd = -1;
static int s_client_fd = -1;
static uint32_t s_node_num = 0;
static TaskHandle_t s_task_handle = NULL;
static SemaphoreHandle_t s_send_mutex = NULL;
static uint8_t s_rx_assembly[TCP_FRAME_MAX];
static uint16_t s_rx_assembly_len = 0;
static uint16_t s_rx_expected_len = 0;
static uint8_t s_rx_state = 0;

static void tcp_task(void *pvParameters);
static esp_err_t open_listener(void);
static void close_listener(void);
static void accept_client(void);
static void close_client(void);
static void drain_recv(void);
static void feed_rx_byte(uint8_t b);
static esp_err_t register_mdns(void);

esp_err_t meshtastic_tcp_init(uint32_t node_num) {
  if (s_is_running) {
    return ESP_ERR_INVALID_STATE;
  }
  s_node_num = node_num;
  s_rx_state = 0;
  s_rx_assembly_len = 0;
  s_rx_expected_len = 0;

  esp_err_t ret = meshtastic_transport_init();
  if (ret != ESP_OK) {
    return ret;
  }

  if (s_send_mutex == NULL) {
    s_send_mutex = xSemaphoreCreateMutex();
    if (s_send_mutex == NULL) {
      ESP_LOGE(TAG, "Failed to create send mutex");
      return ESP_ERR_NO_MEM;
    }
  }

  ret = open_listener();
  if (ret != ESP_OK) {
    return ret;
  }

  if (register_mdns() != ESP_OK) {
    ESP_LOGW(TAG, "mDNS registration failed (continuing)");
  }

  s_is_running = true;
  BaseType_t ok =
      xTaskCreate(tcp_task, "mesh_tcp", TCP_TASK_STACK, NULL, TCP_TASK_PRIO, &s_task_handle);
  if (ok != pdPASS) {
    s_is_running = false;
    close_listener();
    ESP_LOGE(TAG, "Failed to create tcp task");
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG, "TCP listening on port %d", TCP_PORT);
  return ESP_OK;
}

void meshtastic_tcp_stop(void) {
  if (!s_is_running) {
    return;
  }
  s_is_running = false;
  close_client();
  close_listener();
  mdns_service_remove("_meshtastic", "_tcp");
}

bool meshtastic_tcp_is_running(void) {
  return s_is_running;
}

uint8_t meshtastic_tcp_get_client_count(void) {
  return (s_client_fd >= 0) ? 1 : 0;
}

void meshtastic_tcp_send_fromradio(const uint8_t *frame, uint16_t len) {
  if (frame == NULL || len == 0 || len > TCP_FRAME_MAX) {
    return;
  }
  if (s_client_fd < 0 || s_send_mutex == NULL) {
    return;
  }
  if (xSemaphoreTake(s_send_mutex, pdMS_TO_TICKS(TCP_SEND_MUTEX_TIMEOUT_MS)) != pdTRUE) {
    return;
  }

  uint8_t header[TCP_HEADER_LEN];
  header[0] = TCP_FRAME_START1;
  header[1] = TCP_FRAME_START2;
  header[2] = (uint8_t)(len >> 8);
  header[3] = (uint8_t)(len & 0xFF);

  int fd = s_client_fd;
  if (fd >= 0) {
    if (send(fd, header, TCP_HEADER_LEN, 0) < 0 || send(fd, frame, len, 0) < 0) {
      ESP_LOGW(TAG, "send failed errno=%d", errno);
      close_client();
    }
  }

  xSemaphoreGive(s_send_mutex);
}

static void tcp_task(void *pvParameters) {
  (void)pvParameters;
  while (s_is_running) {
    if (s_client_fd < 0) {
      accept_client();
    } else {
      drain_recv();
    }
    vTaskDelay(pdMS_TO_TICKS(TCP_TASK_TICK_MS));
  }
  s_task_handle = NULL;
  vTaskDelete(NULL);
}

static esp_err_t open_listener(void) {
  s_server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s_server_fd < 0) {
    ESP_LOGE(TAG, "socket() failed errno=%d", errno);
    return ESP_FAIL;
  }

  int opt = 1;
  setsockopt(s_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_port = htons(TCP_PORT),
      .sin_addr.s_addr = INADDR_ANY,
  };

  if (bind(s_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    ESP_LOGE(TAG, "bind() failed errno=%d", errno);
    close(s_server_fd);
    s_server_fd = -1;
    return ESP_FAIL;
  }
  if (listen(s_server_fd, 1) < 0) {
    ESP_LOGE(TAG, "listen() failed errno=%d", errno);
    close(s_server_fd);
    s_server_fd = -1;
    return ESP_FAIL;
  }

  int flags = fcntl(s_server_fd, F_GETFL, 0);
  fcntl(s_server_fd, F_SETFL, flags | O_NONBLOCK);
  return ESP_OK;
}

static void close_listener(void) {
  if (s_server_fd >= 0) {
    close(s_server_fd);
    s_server_fd = -1;
  }
}

static void accept_client(void) {
  struct sockaddr_in client_addr;
  socklen_t addr_len = sizeof(client_addr);
  int fd = accept(s_server_fd, (struct sockaddr *)&client_addr, &addr_len);
  if (fd < 0) {
    return;
  }
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  s_client_fd = fd;
  s_rx_state = 0;
  s_rx_assembly_len = 0;
  s_rx_expected_len = 0;
  ESP_LOGI(TAG, "Client connected from %s", inet_ntoa(client_addr.sin_addr));
}

static void close_client(void) {
  if (s_client_fd >= 0) {
    close(s_client_fd);
    s_client_fd = -1;
    ESP_LOGI(TAG, "Client closed");
  }
  s_rx_state = 0;
  s_rx_assembly_len = 0;
  s_rx_expected_len = 0;
}

static void drain_recv(void) {
  uint8_t rx_buf[TCP_RX_BUF_SIZE];
  int n = recv(s_client_fd, rx_buf, sizeof(rx_buf), 0);
  if (n > 0) {
    for (int i = 0; i < n; i++) {
      feed_rx_byte(rx_buf[i]);
    }
    return;
  }
  if (n == 0) {
    close_client();
    return;
  }
  if (errno == EAGAIN || errno == EWOULDBLOCK) {
    return;
  }
  ESP_LOGW(TAG, "recv error errno=%d", errno);
  close_client();
}

static void feed_rx_byte(uint8_t b) {
  switch (s_rx_state) {
    case 0:
      if (b == TCP_FRAME_START1) {
        s_rx_state = 1;
      }
      break;
    case 1:
      if (b == TCP_FRAME_START2) {
        s_rx_state = 2;
      } else {
        s_rx_state = (b == TCP_FRAME_START1) ? 1 : 0;
      }
      break;
    case 2:
      s_rx_expected_len = (uint16_t)b << 8;
      s_rx_state = 3;
      break;
    case 3:
      s_rx_expected_len |= (uint16_t)b;
      s_rx_assembly_len = 0;
      if (s_rx_expected_len == 0 || s_rx_expected_len > TCP_FRAME_MAX) {
        s_rx_state = 0;
      } else {
        s_rx_state = 4;
      }
      break;
    case 4:
      s_rx_assembly[s_rx_assembly_len++] = b;
      if (s_rx_assembly_len >= s_rx_expected_len) {
        meshtastic_transport_send_toradio(s_rx_assembly, s_rx_assembly_len);
        s_rx_state = 0;
      }
      break;
    default:
      s_rx_state = 0;
      break;
  }
}

static esp_err_t register_mdns(void) {
  esp_err_t ret = mdns_init();
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    return ret;
  }
  char hostname[TCP_HOSTNAME_LEN];
  char instance[TCP_INSTANCE_LEN];
  snprintf(hostname, sizeof(hostname), "highboy-%04lx", (unsigned long)(s_node_num & 0xFFFF));
  snprintf(instance, sizeof(instance), "Highboy_%04lx", (unsigned long)(s_node_num & 0xFFFF));
  mdns_hostname_set(hostname);
  mdns_instance_name_set(instance);
  return mdns_service_add(NULL, "_meshtastic", "_tcp", TCP_PORT, NULL, 0);
}
