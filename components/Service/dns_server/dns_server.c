// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#include "esp_log.h"
#include "lwip/sockets.h" // Para sockets UDP
#include "dns_server.h"
#include "lwip/netdb.h"   // Para struct sockaddr_in
#include <string.h>       // Para memset, memcpy

// TAG para logs
static const char *TAG_DNS = "dns_server";

static TaskHandle_t dns_task_handle = NULL;
// IP do ESP32 AP (geralmente 192.168.4.1)
// Certifique-se de que este é o IP real do seu AP!
// Você pode obter isso dinamicamente usando esp_netif_get_ip_info()
#define ESP_AP_IP_ADDR "192.168.4.1"

// Estrutura simplificada para um cabeçalho DNS
typedef struct __attribute__((packed)) {
    uint16_t id;       // Identificador de transação
    uint16_t flags;    // Flags
    uint16_t qdcount;  // Número de perguntas
    uint16_t ancount;  // Número de respostas
    uint16_t nscount;  // Número de autoridades (não usado para nós)
    uint16_t arcount;  // Número de recursos adicionais (não usado para nós)
} dns_header_t;

// Estrutura simplificada para uma pergunta DNS (apenas o tipo e classe para nós)
typedef struct __attribute__((packed)) {
    uint16_t qtype;  // Tipo da pergunta (A=1 para IPv4)
    uint16_t qclass; // Classe da pergunta (IN=1 para Internet)
} dns_question_tail_t;

// Estrutura simplificada para uma resposta DNS (para um registro A)
typedef struct __attribute__((packed)) {
    uint16_t name;     // Ponteiro para o nome no cabeçalho (0xC00C para o primeiro nome na pergunta)
    uint16_t type;     // Tipo do registro (A=1)
    uint16_t class;    // Classe do registro (IN=1)
    uint32_t ttl;      // Time to Live
    uint16_t data_len; // Comprimento dos dados (4 para IPv4)
    uint32_t ip_addr;  // Endereço IP (em network byte order)
} dns_answer_t;


// Função para criar e enviar uma resposta DNS
static void send_dns_response(int sock, struct sockaddr_in *client_addr, socklen_t addr_len,
                              uint8_t *request_buf, int request_len) {

    uint8_t response_buf[512]; // Buffer para a resposta DNS
    memset(response_buf, 0, sizeof(response_buf));

    // 1. Copiar o cabeçalho da requisição para a resposta
    // Isso garante que o ID da transação seja o mesmo
    memcpy(response_buf, request_buf, sizeof(dns_header_t));
    dns_header_t *resp_hdr = (dns_header_t *)response_buf;

    // Ajustar flags para uma resposta:
    // Bit 15 (QR): 1 para resposta
    // Bit 11-8 (Opcode): 0 para consulta padrão
    // Bit 7 (AA): 0 (não autoritativo)
    // Bit 4 (RA): 1 (recursão disponível - embora não recursiva, é padrão para respostas)
    resp_hdr->flags = htons(0x8180); // 0x8180 para uma resposta padrão
    resp_hdr->ancount = htons(1);    // 1 registro de resposta

    // 2. Copiar a seção de pergunta da requisição para a resposta
    // Isso é importante para que o cliente saiba qual pergunta estamos respondendo
    // A seção de pergunta começa logo após o cabeçalho
    uint8_t *question_start = request_buf + sizeof(dns_header_t);
    int question_len_in_req = 0;
    uint8_t *current_ptr = question_start;

    // Calcular o comprimento da string do domínio na pergunta
    while (*current_ptr != 0) {
        question_len_in_req += (*current_ptr) + 1; // Comprimento do label + 1 (para o byte do comprimento)
        current_ptr += (*current_ptr) + 1;
    }
    question_len_in_req += 1; // Para o byte 0 final

    // Adicionar o tamanho do tipo e da classe da pergunta (2+2 bytes)
    question_len_in_req += sizeof(dns_question_tail_t);

    // Copiar a pergunta para o buffer de resposta
    memcpy(response_buf + sizeof(dns_header_t), question_start, question_len_in_req);

    // 3. Adicionar a seção de resposta (Answer)
    dns_answer_t *answer = (dns_answer_t *)(response_buf + sizeof(dns_header_t) + question_len_in_req);

    answer->name = htons(0xC00C); // Ponteiro para o nome na pergunta (0xC00C = 12 bytes do início + 0x0C)
    answer->type = htons(1);      // Tipo A (IPv4)
    answer->class = htons(1);     // Classe IN (Internet)
    answer->ttl = htonl(60);      // TTL de 60 segundos
    answer->data_len = htons(4);  // Comprimento dos dados (4 bytes para IPv4)

    // Converter o IP do ESP32 para network byte order
    inet_pton(AF_INET, ESP_AP_IP_ADDR, &(answer->ip_addr));

    // 4. Enviar a resposta UDP de volta ao cliente
    int response_len = sizeof(dns_header_t) + question_len_in_req + sizeof(dns_answer_t);
    int err = sendto(sock, response_buf, response_len, 0, (struct sockaddr *)client_addr, addr_len);
    if (err < 0) {
        ESP_LOGE(TAG_DNS, "Erro ao enviar resposta DNS: errno %d", errno);
    } else {
        ESP_LOGD(TAG_DNS, "Resposta DNS enviada para %s:%d",
                 inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port));
    }
}

// Tarefa principal do servidor DNS
void dns_server_task(void *pvParameters) {
    char rx_buffer[512]; // Buffer para dados recebidos
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int sock;

    // Configurar o endereço do servidor DNS (o ESP32 AP)
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(53); // Porta padrão DNS
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Escutar em todas as interfaces

    // Criar socket UDP
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG_DNS, "Falha ao criar socket DNS: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    // Ligar o socket ao endereço e porta
    int err = bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (err < 0) {
        ESP_LOGE(TAG_DNS, "Falha ao ligar socket DNS: errno %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG_DNS, "Servidor DNS iniciado na porta 53");

    while (1) {
        // Receber dados do cliente
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&client_addr, &addr_len);
        if (len < 0) {
            ESP_LOGE(TAG_DNS, "Erro ao receber dados DNS: errno %d", errno);
            break;
        }

        rx_buffer[len] = 0; // Terminar a string recebida (se for texto)

        // Verificar se é uma consulta DNS válida
        if (len >= sizeof(dns_header_t) && ((dns_header_t *)rx_buffer)->qdcount > 0) {
            // Processar e enviar a resposta DNS
            send_dns_response(sock, &client_addr, addr_len, (uint8_t *)rx_buffer, len);
        } else {
            ESP_LOGD(TAG_DNS, "Pacote UDP recebido não é uma consulta DNS válida ou muito pequeno.");
        }
    }

    // Se o loop for quebrado, fechar o socket e deletar a tarefa
    close(sock);
    ESP_LOGE(TAG_DNS, "Tarefa do servidor DNS encerrada.");
    vTaskDelete(NULL);
}

void start_dns_server(void) {
  if (dns_task_handle != NULL) {
    ESP_LOGW(TAG_DNS, "DNS server is already run");
    return;
  }

  xTaskCreate(&dns_server_task, "dns_server", 3072, NULL, 5, &dns_task_handle);
  ESP_LOGI(TAG_DNS, "DNS server started");
}

void stop_dns_server(void) {
  if (dns_task_handle != NULL) {
    vTaskDelete(dns_task_handle);
    dns_task_handle = NULL;
    ESP_LOGI(TAG_DNS, "DNS Server stopped and task removed");
  } else {
    ESP_LOGW(TAG_DNS, "DNS Server is already stoped");
  }
}
