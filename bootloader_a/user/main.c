#include "stm32f10x.h"
#include "my_usart.h"
#include "stdio.h"
#include "W25Q64.h"
#include "MySPI.h"



int fputc(int ch, FILE *f)
{
	// 等待发送寄存器为空
	while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET)
		;
	// 发送一字节
	USART_SendData(USART1, (uint8_t)ch);
	// 等待移位发送完成
	while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET)
		;
	return ch;
}
extern usart_data U0CB;
uint16_t n = 0;

uint8_t MID;							//定义用于存放MID号的变量
uint16_t DID;



int main(void)
{
	SystemInit();//初始化单片机系统时钟，让单片机跑在你设定的主频，为后面所有外设（串口、定时器、ADC）提供时钟基础

	SCB->VTOR = 0x08004000;//告诉 CPU：中断向量表不再放在默认地址`0x08000000`，现在搬到`0x08004000`这个位置了
    
	
	
	
	/****************以上很重要***********************/	
	all_init();
	
	printf("\r\n成功跳转\r\n");
	
	
	

	
	
	
	
	while (1)
	{
		if(U0CB.URxDataOUT != U0CB.URxDataIN) // 不相等说明缓冲区中有数据了
		{
			u1_printf("本次接收了%d个字节\r\n", U0CB.URxDataOUT->end - U0CB.URxDataOUT->start + 1);
			for (n = 0; n < U0CB.URxDataOUT->end - U0CB.URxDataOUT->start + 1; n++)
			{
				u1_printf("%c ", U0CB.URxDataOUT->start[n]);
			}
			u1_printf("\r\n\r\n");

			U0CB.URxDataOUT++;
			if (U0CB.URxDataOUT == U0CB.URxDataEND)
			{
				U0CB.URxDataOUT = &U0CB.URxDataPtr[0];
			}
		}
		
	}
}
