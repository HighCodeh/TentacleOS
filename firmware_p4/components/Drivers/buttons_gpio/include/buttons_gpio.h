#ifndef  BUTTONS_GPIO_H
#define  BUTTONS_GPIO_H

#include <stdbool.h>
#include <stdint.h>
#include "pin_def.h"
typedef struct {
    uint32_t gpio;
    bool     last_state;
    bool     pressed_flag;   
} button_t;



void buttons_init(void);
bool up_button_pressed(void); 
bool down_button_pressed(void); 
bool left_button_pressed(void);  
bool right_button_pressed(void); 
bool ok_button_pressed(void);    
bool back_button_pressed(void);  

bool up_button_is_down(void);    
bool down_button_is_down(void); 
bool left_button_is_down(void);  
bool right_button_is_down(void); 
bool ok_button_is_down(void);    
bool back_button_is_down(void);  


#endif // BUTTONS_GPIO_H
