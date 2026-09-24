#ifndef __FLASH_H
#define __FLASH_H
#include "stm32f10x.h"                  // Device header
#include <stdint.h>

void STM32_EraseFlash(uint32_t start, uint16_t num);


void STM32_WriteFlash(uint32_t saddr, uint32_t *wdata, uint32_t wnum);


#endif

