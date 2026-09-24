#ifndef __MY_USART_H
#define __MY_USART_H

#include "stdint.h"
#include "my_usart.h"
#include "stdarg.h"
#include "stdio.h"
#include "string.h"
#include "delay.h"

void all_init(void);
/**
 * @brief 串口接收缓冲区大小
 * 
 */
#define usart_rx_buf_size 2048

#define usart_tx_buf_size 2048

/**
 * @brief 串口单次接收缓冲区接收数据最大长度
 * 
 */
#define usart_rx_buf_max 256

/**
 * @brief start end 数组长度
 * 
 *
 */
#define num 10

    /**
     * @brief 串口接收缓冲区结构体开始结束指针
     *
     */
    typedef struct
{
    uint8_t *start;
    uint8_t *end;
} usart_rx_ct;

/**
 * @brief 串口接收缓冲区结构体
 * 
 */
typedef struct
{
    uint16_t URxCounter;// 缓冲区已经存放的数据量
    usart_rx_ct URxDataPtr[num];//定义的有几个start end
    usart_rx_ct *URxDataIN;
    usart_rx_ct *URxDataOUT;
    usart_rx_ct *URxDataEND;
} usart_data;

void UORx_PtrInit(void);
void u1_printf(const char *fmt, ...);
void my_usart1_init(void);
void USART1_IRQHandler(void);


#endif

