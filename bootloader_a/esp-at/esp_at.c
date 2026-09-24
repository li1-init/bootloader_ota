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
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include "main.h"
#include "esp_usart.h"
#include "esp_at.h"

/* 串口接收缓存给4K大小 */
#define RX_BUFFER_SIZE 4096

#define RX_RESULT_OK 0    /* AT命令返回OK */
#define RX_RESULT_ERROR 1 /* AT命令返回ERROR */
#define RX_RESULT_FAIL 2  /* 数据接收错误，缓存区满 */

static uint8_t rxdata[RX_BUFFER_SIZE]; /* 接收缓存 */
static uint32_t rxlen;                 /* 接收数据长度 */
static bool rxready;                   /* rxready=true允许接收数据，防止异常数据进入rxdata */
static uint8_t rxresult;               /* 接收结果 */

/**
 * @brief 串口接收回调函数
 *
 * @param data 串口接收到的数据
 */
static void on_usart_received(uint8_t data)
{
    /* 接收数据标志位为false，不接收数据，只有MCU发了AT命令后，才会开启rxready准备接收数据 */
    if (!rxready)
    {
        return;
    }

    /* 接收数据长度必须小于缓存区大小 */
    if (rxlen < RX_BUFFER_SIZE)
    {
        /* 接收数据 */
        rxdata[rxlen++] = data;
    }
    else
    {
        /* 缓存区满，接收失败 */
        rxresult = RX_RESULT_FAIL;
        rxready = false;
        return;
    }
    /* 判断读到换行符'\n'，AT模组返回报文结尾是\r\n（回车+换行） */
    /* 判断是否接收到换行符，表示数据接收完毕 */
    if (data == '\n')
    {
        /* 判断接收换行符是否为\r\n */
        if (rxlen >= 2 && rxdata[rxlen - 2] == '\r')
        {
            /*
             * 判断应答结尾是不是 "OK\r\n"
             * rxlen‑4 位置对应'O'，rxlen‑3对应'K'，后面跟着\r\n
             * 例：... O K \r \n
             */
            /* 判断收到的是否是OK */
            if (rxlen >= 4 &&
                rxdata[rxlen - 4] == 'O' && rxdata[rxlen - 3] == 'K')
            {
                /* AT命令执行成功 */
                /* AT命令执行成功，结果状态置OK，关闭接收使能 */
                rxresult = RX_RESULT_OK;
                rxready = false;
            }
            /* 判断收到的是否是ERROR */ /* 判断应答结尾是不是 "ERROR\r\n" */
            else if (rxlen >= 7 &&
                     rxdata[rxlen - 5] == 'E' && rxdata[rxlen - 4] == 'R' &&
                     rxdata[rxlen - 3] == 'R' && rxdata[rxlen - 2] == 'O' &&
                     rxdata[rxlen - 1] == 'R')
            {
                /* AT指令执行出错，状态标记ERROR，关闭接收 */
                rxresult = RX_RESULT_ERROR;
                rxready = false;
            }
        }
    }
}

/**
 * @brief 初始化esp32的at通信模块，包括串口初始化、AT命令执行结果回调函数注册
 *
 * @return true 初始化成功
 * @return false 初始化失败
 */
bool esp_at_init(void)
{
    /* rxready置false，上电默认不接收ESP模组发来数据 */
    rxready = false;

    /* 初始化串口2，波特率115200，PA2 PA3 */
    esp_usart_init();
    /* 注册串口接收回调函数 */
    esp_usart_receive_register(on_usart_received);

    /* 发送复位esp32命令，重置esp32 */
    return esp_at_reset();
}

/**
 * @brief 发送AT命令到esp32
 *
 * @param cmd AT命令字符串
 * @param rsp AT命令执行结果
 * @param length AT命令执行结果长度
 * @param timeout AT命令执行超时时间
 * @return true AT命令执行成功
 * @return false AT命令执行失败
 */

/****
 * rsp   length   存在的目的主要是通过指针的方式来修改main()中的变量
 */
bool esp_at_send_command(const char *cmd, const char **rsp, uint32_t *length, uint32_t timeout)
{
    rxlen = 0;                 /* rxlen=0，接收数据长度清零 */
    rxready = true;            /* rxready=true，准备接收数据 */
    rxresult = RX_RESULT_FAIL; /* rxresult=RX_RESULT_FAIL，先预置接收结果失败 */

    /* 串口输出AT指令，AT指令结尾必须带上\r\n回车换行 */
    esp_usart_write_string(cmd);
    esp_usart_write_string("\r\n");

    /*
     * 等待应答循环：
     * rxready==true：还没收到结束标志OK/ERROR；
     * timeout-- 做超时倒计时，每轮delay_ms(1)，单位ms。
     * 两种退出循环：①收到OK/ERROR回调把rxready置false；②timeout减到0超时退出。
     */
    while (rxready && timeout--)
    {
        delay_ms(1);
    }
    rxready = false; /* rxready=false，无论接收成功or失败or超时，都不再接收数据了 */

    /* 如果rsp不为空，将接收到的数据返回 */
    if (rsp)
    {
        *rsp = (const char *)rxdata;
    }
    // rsp是二级指针 const char **rsp
    // 如果调用方传进来不是NULL，把全局接收缓冲区rxdata的首地址赋值给外部指针变量。
    // 外部就可以读取模组返回应答字符串；传NULL，这段代码不会执行，拿不到应答
    if (length)
    {
        *length = rxlen;
    }
    // length是uint32_t*指针；不为NULL，就把本次收到字节数rxlen给到外部变量。
    // 不需要长度就传入NULL，代码不执行。

    return rxresult == RX_RESULT_OK;
    // 返回bool：rxresult等于RX_RESULT_OK 返回true（AT执行成功收到OK），否则返回false（超时/返回ERROR）
}

/**
 * @brief 发送数据到esp32
 *
 * @param data 待发送的数据
 * @param length 待发送数据的长度
 * @return true 发送成功
 * @return false 发送失败（不会出现失败情况）
 */
bool esp_at_send_data(const uint8_t *data, uint32_t length)
{
    esp_usart_write_data((uint8_t *)data, length);

    return true;
}

/**
 * @brief 复位esp32
 *
 * @return true 复位成功
 * @return false 复位失败
 */
bool esp_at_reset(void)
{
    /* 复位esp32命令 */
    if (!esp_at_send_command("AT+RESTORE", NULL, NULL, 1000))
    {
        return false;
    }
    delay_ms(2000); /* 等待esp32重启 */

    /* 关闭回显 */
    if (!esp_at_send_command("ATE0", NULL, NULL, 1000))
    {
        return false;
    }

    /* 关闭配置储存 */
    if (!esp_at_send_command("AT+SYSSTORE=0", NULL, NULL, 1000))
    {
        return false;
    }

    return true;
}

/**
 * @brief 初始化esp32的wifi模块，设置为station模式
 *
 * @return true 初始化成功
 * @return false 初始化失败
 */
bool esp_at_wifi_init(void)
{
    /* 设置为station模式 */
    if (!esp_at_send_command("AT+CWMODE=1", NULL, NULL, 1000))
    {
        return false;
    }

    // 设置WiFi发射功率参数40（10dBm）
    if (!esp_at_send_command("AT+RFPOWER=40", NULL, NULL, 1000))
    {
        return false;
    }

    return true;
}

/**
 * @brief 连接wifi
 *
 * @param ssid wifi名称
 * @param pwd wifi密码
 * @return true 连接成功
 * @return false 连接失败
 */
bool esp_at_wifi_connect(const char *ssid, const char *pwd)
{
    char cmd[64];

    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, pwd);
    if (!esp_at_send_command(cmd, NULL, NULL, 10000))
    {
        return false;
    }

    return true;
}

/**
 * @brief 获取http数据
 *
 * @param url http地址
 * @param rsp http响应数据
 * @param length http响应数据长度
 * @param timeout 超时时间
 * @return true 获取成功
 * @return false 获取失败
 */
bool esp_at_get_http(const char *url, const char **rsp, uint32_t *length, uint32_t timeout)
{
    char cmd[128];

    snprintf(cmd, sizeof(cmd), "AT+HTTPCGET=\"%s\"", url);
    if (!esp_at_send_command(cmd, rsp, length, 10000))
    {
        return false;
    }

    return true;
}

/**
 * @brief 初始化sntp时间
 *
 * @return true 初始化成功
 * @return false 初始化失败
 */
bool esp_at_sntp_init(void)
{
    /* 设置sntp服务器 */
    if (!esp_at_send_command("AT+CIPSNTPCFG=1,8,\"cn.ntp.org.cn\",\"ntp.sjtu.edu.cn\"", NULL, NULL, 1000))
    {
        return false;
    }

    /* 从sntp服务器获取时间，此时会校准esp32的rtc时间 */
    if (!esp_at_send_command("AT+CIPSNTPTIME?", NULL, NULL, 1000))
    {
        return false;
    }

    return true;
}

/**
 * @brief 从esp32获取时间戳
 *
 * @param timestamp 时间戳
 * @return true 获取成功
 * @return false 获取失败
 */
bool esp_at_get_time(uint32_t *timestamp)
{
    const char *rsp;
    uint32_t length;

    if (!esp_at_send_command("AT+SYSTIMESTAMP?", &rsp, &length, 1000))
    {
        return false;
    }

    /*****
     * strstr (字符串，要找的子串)
    在 rsp 指向的字符串里面，找到子串`"+SYSTIMESTAMP:"`第一次出现的位置，返回这个位置的指针。
    strlen 算出字符串`"+SYSTIMESTAMP:"`一共多少字符长度。
    指针做加法，指针向后偏移，跳过标签文本，直接跳到数字的首字符。
     */
    char *sts = strstr(rsp, "+SYSTIMESTAMP:") + strlen("+SYSTIMESTAMP:");

    *timestamp = atoi(sts); // atoi 读取 sts 指向的字符串，把文本形式的数字字符串 `"1758211234"`转换成真正整型数字 1758211234。

    return true;
}

/**
 * @brief 获取wifi的ip地址，ip地址格式为xxx.xxx.xxx.xxx
 *
 * @param ip ip地址
 * @return true 获取成功
 * @return false 获取失败
 */
bool esp_at_wifi_get_ip(char ip[16])
{
    const char *rsp;
    if (!esp_at_send_command("AT+CIPSTA?", &rsp, NULL, 1000))
    {
        return false;
    }

    /* 解析ip地址，响应示例：+CIPSTA:ip:192.168.1.1 */
    const char *pip = strstr(rsp, "+CIPSTA:ip:") + strlen("+CIPSTA:ip:");

    /* 如果响应中有ip地址，则拷贝到ip中 */
    if (pip)
    {
        for (int i = 0; i < 16; i++)
        {
            if (pip[i] == '\r')
            {
                /***
                 * - pip [i] 读到`'\r'`，代表 IP 文本读完了，后面`\r`不属于 IP 地址，不能拷贝进去。
- 把当前数组位置赋值`ip[i] = '\0'`，手动加上字符串结束标记，截断字符串。
- break 跳出 for 循环，不再拷贝后面字符。
                 */
                ip[i] = '\0';
                break;
            }
            ip[i] = pip[i];
        }
        return true;
    }

    return true;
}

/**
 * @brief 获取wifi的mac地址，mac地址格式为xx:xx:xx:xx:xx:xx
 *
 * @param mac mac地址
 * @return true 获取成功
 * @return false 获取失败
 */
bool esp_at_wifi_get_mac(char mac[18])
{
    const char *rsp;
    if (!esp_at_send_command("AT+CIPSTAMAC?", &rsp, NULL, 1000))
    {
        return false;
    }

    /* 解析mac地址，响应示例：+CIPSTAMAC:mac:00:11:22:33:44:55 */
    const char *pmac = strstr(rsp, "+CIPSTAMAC:mac:") + strlen("+CIPSTAMAC:mac:");
    /* 如果响应中有mac地址，则拷贝到mac中 */
    if (pmac)
    {
        strncpy(mac, pmac, 18);
        return true;
    }

    return true;
}
