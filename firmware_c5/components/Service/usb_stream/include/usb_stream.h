#ifndef USB_STREAM_H
#define USB_STREAM_H

/**
 * @brief Envia o conteúdo completo do framebuffer do ST7789 pela porta USB CDC.
 * * Esta função obtém o ponteiro para o framebuffer, escreve seus bytes brutos
 * para a saída padrão (stdout), que deve ser configurada para USB CDC no menuconfig,
 * e força o envio dos dados.
 */
void send_framebuffer_over_usb(void);

#endif // USB_STREAM_H