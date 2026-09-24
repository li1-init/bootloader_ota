#ifndef __STRUCT_INIT_H
#define __STRUCT_INIT_H

#include "stm32f10x.h"
#define U0_RX_SIZE 2048

void gpio_initstruct(void);
void usart1_debug(void);
void my_usart1_init(void);
void tim3_init(void);
void TIM3_IRQHandler(void);
uint32_t get_tick(void);
#endif
