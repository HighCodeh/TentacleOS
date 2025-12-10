#ifndef LV_PORT_INDEV_H
#define LV_PORT_INDEV_H

#include "lvgl.h"

// Ponteiro para o dispositivo de entrada (Teclado)
// Necessário para adicionar a um Grupo LVGL (lv_group_add_indev)
extern lv_indev_t * indev_keypad;
extern lv_group_t * main_group;


// Inicializa a camada de input
void lv_port_indev_init(void);

#endif // LV_PORT_INDEV_H
