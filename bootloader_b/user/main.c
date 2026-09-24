
#include "stm32f10x.h"
#include "my_usart.h"
#include "stdio.h"
#include "W25Q64.h"
#include "MySPI.h"
#include "bootloader.h"
#include "usart_command.h"
#include "esp_at.h"
#include "esp_wifi.h"
#include "esp_mqtt.h"


/* ------------------- printf重定向 ------------------- */
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

/* ------------------- 变量声明 ------------------- */
extern usart_data U0CB;
uint16_t n = 0;

int main(void)
{

    all_init();

    wifi_int();

    mqtt_connect();

    uasrt_command_suf();

    while (1)
    {
        /* ------------------- 接收数据 ------------------- */
        if (U0CB.URxDataOUT != U0CB.URxDataIN) // 不相等说明缓冲区中有数据了
        {

            bootloader_event(U0CB.URxDataOUT->start, U0CB.URxDataOUT->end - U0CB.URxDataOUT->start + 1);

            /* ------------------- 数据弹出移动 ------------------- */
            U0CB.URxDataOUT++;
            if (U0CB.URxDataOUT == U0CB.URxDataEND)
            {
                U0CB.URxDataOUT = &U0CB.URxDataPtr[0];
            }
        }
    }
}
