/*
 * MIT License
 *
 * Copyright (c) 2023 梅花嵌入式
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "stm32f10x.h"
#include "esp_usart.h"


/* 串口接收回调函数的函数指针 */
static esp_usart_receive_callback_t esp_usart_receive_callback;


/**
 * @brief 串口初始化
 *
 */
void esp_usart_init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);  // GPIOA时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE); // USART2在APB1总线！！
    GPIO_InitTypeDef GPIO_InitStructure;

    /* PA2 TX */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* PA3 RX */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* 串口2中断优先级为5 */
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 5;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* 串口2 波特率115200 没有流控 */
    USART_InitTypeDef USART_InitStructure;
    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;

    /* 初始化串口2，启用RX中断 */
    USART_Init(USART2, &USART_InitStructure);
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE); // 开启接收非空中断：收到1字节数据触发中断（寄存器只能放1字节）
    USART_Cmd(USART2, ENABLE);
}

/**
 * @brief 串口发送数据
 *
 * @param data 待发送的数据
 * @param length 待发送数据的长度
 */
void esp_usart_write_data(uint8_t *data, uint16_t length)
{
    for (uint16_t i = 0; i < length; i++)
    {
        /* 等待发送缓冲区为空，即等待上一个字节通过串口发送完毕 */
        while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
        USART_SendData(USART2, data[i]); // 将1字节写入发送寄存器，硬件自动移位输出
    }
}

/**
 * @brief 串口发送字符串
 *
 * @param str 待发送的字符串
 */
void esp_usart_write_string(const char *str)
{
    uint16_t len = strlen(str); // strlen计算字符串长度，不计末尾'\0'
    esp_usart_write_data((uint8_t *)str, len);
}

/**
 * @brief 注册串口接收回调函数
 *
 * @param callback 串口接收回调函数
 */
void esp_usart_receive_register(esp_usart_receive_callback_t callback)
{
    esp_usart_receive_callback = callback;
}

/**
 * @brief 串口2中断服务函数
 *
 */
void USART2_IRQHandler(void)
{
    /* 当RXNE=1时，表示接收到数据 */
    if(USART_GetITStatus(USART2, USART_IT_RXNE) == SET)
    {
        /* 读取接收到的数据 */
        uint8_t data = USART_ReceiveData(USART2);

        /* 如果有注册串口接收回调函数，则调用 */
        if (esp_usart_receive_callback)
            esp_usart_receive_callback(data);
    }
}
