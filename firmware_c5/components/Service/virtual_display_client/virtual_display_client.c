#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h" // Incluir para semáforos
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h" // Para timeouts
#include "st7789.h"
#include "virtual_display_client.h" // Incluir o próprio cabeçalho

static const char *TAG = "VIRTUAL_DISPLAY";
static int sock = -1; // Socket global

// Declaração da tarefa para que possa ser chamada novamente
void connect_to_server_task(void *pvParameters);
static TaskHandle_t reconnect_task_handle = NULL;

// Semáforo para sinalizar que um novo frame está pronto
static xSemaphoreHandle s_frame_ready_semaphore = NULL; 
static TaskHandle_t s_frame_sender_task_handle = NULL; // Handle para a tarefa de envio de frames

// Protótipo da tarefa de envio de frames
static void frame_sender_task(void *pvParameters);

// Função que lida com os eventos do Wi-Fi (agora pública e renomeada)
void virtual_display_wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Reconectando ao Wi-Fi...");
        if (sock >= 0) {
            close(sock);
            sock = -1;
        }
        // Se a tarefa de reconexão estiver ativa, pare-a antes de tentar conectar novamente.
        if (reconnect_task_handle != NULL) {
            vTaskDelete(reconnect_task_handle);
            reconnect_task_handle = NULL;
        }
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Conectado! IP do Highboy: " IPSTR, IP2STR(&event->ip_info.ip));
        // Agora que temos um IP, podemos iniciar a tarefa de conexão ao servidor
        xTaskCreate(connect_to_server_task, "connect_task", 4096, NULL, 5, &reconnect_task_handle);
    }
}

// Tarefa que tenta se conectar ao servidor em um loop
void connect_to_server_task(void *pvParameters) {
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(SERVER_IP_ADDR_VIRTUAL_DISPLAY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(SERVER_PORT_VIRTUAL_DISPLAY);
    while (1) {
        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (sock < 0) {
            ESP_LOGE(TAG, "Não foi possível criar o socket: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        // Define um timeout para o envio de dados
        struct timeval sending_timeout;
        sending_timeout.tv_sec = 5;
        sending_timeout.tv_usec = 0;
        if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &sending_timeout, sizeof(sending_timeout)) < 0) {
            ESP_LOGE(TAG, "Falha ao definir timeout de envio: errno %d", errno);
            close(sock);
            sock = -1;
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }


        
ESP_LOGI(TAG, "Socket criado, conectando a %s:%d", SERVER_IP_ADDR_VIRTUAL_DISPLAY, SERVER_PORT_VIRTUAL_DISPLAY);


        int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        
        if (err != 0) {
            ESP_LOGE(TAG, "Falha na conexão do socket: errno %d. Tentando novamente...", errno);
            close(sock);
            sock = -1;
            vTaskDelay(pdMS_TO_TICKS(5000)); // Espera 5s antes de tentar de novo
        } else {
            ESP_LOGI(TAG, "Conectado com sucesso ao servidor!");
            // Inicia a tarefa de envio de frames APENAS UMA VEZ após a conexão bem-sucedida
            if (s_frame_sender_task_handle == NULL) { 
                xTaskCreate(frame_sender_task, "frame_sender", 4096, NULL, 4, &s_frame_sender_task_handle);
            }
            break; // Sai do loop de tentativas de conexão
        }
    }
    reconnect_task_handle = NULL; // Limpa o handle da tarefa
    vTaskDelete(NULL); // Deleta a tarefa após conectar com sucesso
}


// Função para enviar o framebuffer via socket (AGORA CHAMADA PELA TAREFA DEDICADA)
void send_framebuffer_to_server() {
    if (sock < 0) {
        // Se não estiver conectado e não houver uma tarefa de reconexão rodando, cria uma.
        if (reconnect_task_handle == NULL) {
             xTaskCreate(connect_to_server_task, "reconnect_task", 4096, NULL, 5, &reconnect_task_handle);
        }
        return; // Não envia se não houver socket
    }

    uint16_t* fb = st7789_get_framebuffer();
    if (fb == NULL) {
        ESP_LOGE(TAG, "Framebuffer é nulo, não é possível enviar.");
        return;
    }

    int bytes_a_enviar = ST7789_WIDTH * ST7789_HEIGHT * 2;
    int bytes_enviados_total = 0;

    // Loop para garantir que todos os bytes sejam enviados
    while (bytes_enviados_total < bytes_a_enviar) {
        int bytes_enviados = send(sock, (uint8_t*)fb + bytes_enviados_total, bytes_a_enviar - bytes_enviados_total, 0);

        if (bytes_enviados < 0) {
            // Um erro ocorreu. Verificamos se é um erro de "tente novamente" ou um erro real.
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                ESP_LOGW(TAG, "Socket buffer cheio. Tentando novamente em breve.");
                vTaskDelay(pdMS_TO_TICKS(50)); // Pequena pausa para o buffer de rede esvaziar
                continue; // Tenta enviar o mesmo pedaço novamente
            }
            
            ESP_LOGE(TAG, "Erro ao enviar dados: errno %d. Fechando socket.", errno);
            close(sock);
            sock = -1; // Sinaliza que a conexão foi perdida
            // Tenta reconectar em uma nova tarefa para não bloquear a frame_sender_task.
            if (reconnect_task_handle == NULL) {
                xTaskCreate(connect_to_server_task, "reconnect_task", 4096, NULL, 5, &reconnect_task_handle);
            }
            return; // Sai da função de envio
        }
        
        bytes_enviados_total += bytes_enviados;
    }
}

// NOVA TAREFA DEDICADA PARA ENVIAR FRAMES
static void frame_sender_task(void *pvParameters) {
    while (1) {
        // Espera indefinidamente por um sinal de que um novo frame está pronto
        if (xSemaphoreTake(s_frame_ready_semaphore, portMAX_DELAY) == pdTRUE) {
            send_framebuffer_to_server(); // Chama a função de envio, que pode bloquear esta tarefa, mas não a principal
            
            // Opcional: Adicione um pequeno atraso aqui para controlar o FPS de envio
            // Por exemplo, para enviar no máximo 30 frames por segundo (aprox. 33ms por frame)
            // vTaskDelay(pdMS_TO_TICKS(33)); 
        }
    }
}

// Inicializa o semáforo (chamar uma vez na inicialização do Wi-Fi)
void virtual_display_start_frame_sending(void) {
    if (s_frame_ready_semaphore == NULL) {
        s_frame_ready_semaphore = xSemaphoreCreateBinary();
        if (s_frame_ready_semaphore == NULL) {
            ESP_LOGE(TAG, "Falha ao criar o semáforo de frame pronto.");
            return;
        }
    }
    // A tarefa de envio de frames será criada em connect_to_server_task quando a conexão for estabelecida.
    // Isso garante que a tarefa só inicie após termos um IP.
}

// Notifica a tarefa de envio de frames que um novo frame está pronto
void virtual_display_notify_frame_ready(void) {
    if (s_frame_ready_semaphore != NULL) {
        // Libera o semáforo para a tarefa de envio de frames
        // xSemaphoreGiveFromISR pode ser usado se for chamado de uma ISR (interrupção)
        // Mas para chamadas de tarefas normais, xSemaphoreGive é suficiente
        xSemaphoreGive(s_frame_ready_semaphore);
    }
}