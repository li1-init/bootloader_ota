#ifndef __USART_COMMAND_H
#define __USART_COMMAND_H

#include "stm32f10x.h"



/* ------------------ 函数声明 ------------------- */
void uasrt_command_suf(void);

uint8_t uasrt_command(uint32_t time);

void uasrt_command_test(void);

void bootloader_event (uint8_t *data, uint16_t datalen);

void STM32_EraseFlash(uint32_t start_page, uint32_t page_cnt);

void stm32_write_flash(uint32_t addr, uint8_t *pbuf, uint16_t len);


void menu1(void);

void menu2(void);

void menu3(void);

void menu4(void);

void menu5(void);

void menu6(void);

void menu7(void);

void menu8(void);

uint16_t Xmodem_CRC16(uint8_t *data, uint16_t datalen);

void crc16_test(void);



#endif
