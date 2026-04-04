#ifndef VIRTUAL_DISPLAY_CLIENT_H
#define VIRTUAL_DISPLAY_CLIENT_H

#include "esp_event.h" // Necessário para esp_event_base_t e int32_t

// =========================================================================
//  ↓↓↓  CONFIGURAÇÕES DO CLIENTE DE DISPLAY VIRTUAL  ↓↓↓
//  Estas configurações são movidas para o cabeçalho para acesso global
// =========================================================================
#define WIFI_SSID_VIRTUAL_DISPLAY      "SathoshiN" // SSID para o cliente conectar
#define WIFI_PASS_VIRTUAL_DISPLAY      "2008bitc"  // Senha para o cliente conectar
#define SERVER_IP_ADDR_VIRTUAL_DISPLAY "192.168.0.13" // Endereço IP do servidor do display virtual
#define SERVER_PORT_VIRTUAL_DISPLAY    1337      // Porta do servidor do display virtual
// =========================================================================

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicia a tarefa de envio de frames para o display virtual.
 * Esta função deve ser chamada para começar a enviar o framebuffer via Wi-Fi.
 */
void virtual_display_start_frame_sending(void);

/**
 * @brief Notifica que um novo frame está pronto para ser enviado.
 * Deve ser chamada sempre que o framebuffer for atualizado.
 */
void virtual_display_notify_frame_ready(void);

/**
 * @brief Handler de eventos Wi-Fi para o cliente de display virtual.
 * Gerencia a conexão Wi-Fi e a reconexão com o servidor.
 *
 * @param arg Argumento passado para o handler.
 * @param event_base Base do evento (ex: WIFI_EVENT, IP_EVENT).
 * @param event_id ID do evento (ex: WIFI_EVENT_STA_START, IP_EVENT_STA_GOT_IP).
 * @param event_data Dados específicos do evento.
 */
void virtual_display_wifi_event_handler(void* arg, esp_event_base_t event_base,
                                        int32_t event_id, void* event_data);

#ifdef __cplusplus
}
#endif

#endif // VIRTUAL_DISPLAY_CLIENT_H