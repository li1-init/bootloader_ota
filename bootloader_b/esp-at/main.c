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
 *
 *
 *
 * **宽：128px（X 轴横向）
 * **高：160px（Y 轴纵向）
 */

#include <stdbool.h> /* 用尖括号<>引用标准库用 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "stm32f10x.h" /* 用双引号""引用自定义库 */
#include "main.h"
#include "led.h"

#include "rtc.h"
#include "timer.h"
#include "esp_at.h"
#include "mpu6050.h"
#include "st7735.h"
#include "stfonts.h"
#include "stimage.h"
#include "weather.h"

/* ------------------- tim3声明 ------------------- */
volatile uint32_t now = 0;

void tim3_init(void)
{

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    TIM_TimeBaseInitTypeDef timeinit = {0};
    timeinit.TIM_ClockDivision = TIM_CKD_DIV1;
    timeinit.TIM_CounterMode = TIM_CounterMode_Up;
    timeinit.TIM_Period = 999;
    timeinit.TIM_Prescaler = 71;
    timeinit.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM3, &timeinit);
    TIM_ARRPreloadConfig(TIM3, ENABLE);
    TIM_Cmd(TIM3, ENABLE);
    TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);

    NVIC_InitTypeDef nvic_init = {0};
    nvic_init.NVIC_IRQChannel = TIM3_IRQn;
    nvic_init.NVIC_IRQChannelCmd = ENABLE;
    nvic_init.NVIC_IRQChannelPreemptionPriority = 1;
    nvic_init.NVIC_IRQChannelSubPriority = 1;
    NVIC_Init(&nvic_init);
}

void TIM3_IRQHandler(void)
{
    if (TIM_GetFlagStatus(TIM3, TIM_FLAG_Update) == SET)
    {
        now++;
        TIM_ClearFlag(TIM3, TIM_FLAG_Update);
    }
}

uint32_t get_tick(void)
{
    return now;
}

extern const unsigned char hz_ni[];
extern const unsigned char hz_hao[];
extern const unsigned char hz_shi[];
extern const unsigned char hz_jie[];
/* WIFI名称 */
static const char *wifi_ssid = "qwe";
/* WIFI密码 */
static const char *wifi_password = "12345678911";
/* 心知天气获取本地天气的URL链接 */
static const char *weather_uri = "https://api.seniverse.com/v3/weather/now.json?key=SLr5bqkrCRA_AX4Ro&location=wuhan&language=en&unit=c";

/* 运行计数器，每1s自增1 */
static uint32_t runms;
/* 显示屏待显示内容的Y坐标 */
static uint32_t disp_height;

/**
 * @brief 1ms定时器回调，提供毫秒计数值给主程序使用
 * 计数器超过1天就重置
 *
 */
static void timer_elapsed_callback(void)
{
    runms++;
    /* 24小时 x 60分钟 x 60秒 x 1000毫秒 */
    if (runms > 24 * 60 * 60 * 1000)
    {
        /* 计数器归零 */
        runms = 0;
    }
}

/**
 * @brief 初始化wifi模块，并显示初始化信息在屏幕上
 *
 */
static void wifi_init(void)
{
    /* ST7735 LCD屏幕打印Init ESP32...日志 */
    st7735_write_string(0, disp_height, "Init ESP32...", &font_ascii_8x16, ST7735_WHITE, ST7735_BLACK);
    /* 更新下一条显示日志的Y坐标 */
    disp_height += font_ascii_8x16.height;
    /* 1、初始化ESP32_AT功能，主要是初始化主控和ESP32C3模块之间的串口 */
    if (!esp_at_init())
    {
        /* 初始化失败，屏幕用红色打印Failed!!!提示 */
        st7735_write_string(0, disp_height, "Failed!!!", &font_ascii_8x16, ST7735_RED, ST7735_BLACK);
        disp_height += font_ascii_8x16.height;
        /* 初始化失败，程序停留在while(1)处不允许继续运行后续代码，用户需要主动重启设备 */
        while (1)
            ;
    }

    st7735_write_string(0, disp_height, "Init WIFI...", &font_ascii_8x16, ST7735_WHITE, ST7735_BLACK);
    disp_height += font_ascii_8x16.height;
    /* 2、初始化esp32的wifi模块，设置wifi为station模式 */
    if (!esp_at_wifi_init())
    {
        st7735_write_string(0, disp_height, "Failed!!!", &font_ascii_8x16, ST7735_RED, ST7735_BLACK);
        disp_height += font_ascii_8x16.height;
        while (1)
            ;
    }

    st7735_write_string(0, disp_height, "Connect WIFI...", &font_ascii_8x16, ST7735_WHITE, ST7735_BLACK);
    disp_height += font_ascii_8x16.height;
    /* 3、配置esp32模块的wifi ssid和passwd，让esp32连接到wifi */
    if (!esp_at_wifi_connect(wifi_ssid, wifi_password))
    {
        st7735_write_string(0, disp_height, "Failed!!!", &font_ascii_8x16, ST7735_RED, ST7735_BLACK);
        disp_height += font_ascii_8x16.height;
        while (1)
            ;
    }

    st7735_write_string(0, disp_height, "Sync Time...", &font_ascii_8x16, ST7735_WHITE, ST7735_BLACK);
    disp_height += font_ascii_8x16.height;
    /* 4、设置esp32的sntp模块，sntp是ntp（Network Time Protocol）的简化版，用来使进行时钟同步，从互联网上获取当前的年月日时分秒 */
    if (!esp_at_sntp_init())
    {
        st7735_write_string(0, disp_height, "Failed!!!", &font_ascii_8x16, ST7735_RED, ST7735_BLACK);
        disp_height += font_ascii_8x16.height;
        while (1)
            ;
    }
}

void Delay_ms(uint32_t ms)
{
    uint32_t i;
    while (ms--)
        for (i = 0; i < 7200; i++)
            ;
}

static uint8_t count = 0;

void update(void)
{
    char str[32];
    for (count = 0; count < 1; count++)
    {

        /* esp32从sntp服务器读取当前时间，并校准stm32f4的rtc时间 */
        uint32_t ts;
        bool sntp_ok = esp_at_get_time(&ts); // 在这里定义bool变量
        if (sntp_ok)                         // 只有获取时间成功，才设置RTC
        {
            rtc_set_timestamp(ts + 8 * 60 * 60); // +8小时 东八区
        }

        /* 从STM32F4的RTC模块读取年月日时分秒 */
        rtc_date_t date;
        rtc_get_date(&date);
        /* 样例：2024年10月18日则显示：24-10-18 */
        snprintf(str, sizeof(str), "%02d-%02d-%02d", date.year % 100, date.month, date.day);
        st7735_write_string(0, 0, str, &font_ascii_8x16, ST7735_WHITE, ST7735_BLACK);
        /* 样例：12:34，并且中间的":"会1s闪烁一次 */
        snprintf(str, sizeof(str), "%02d%s%02d", date.hour, date.second % 2 ? " " : ":", date.minute);
        st7735_write_string(0, 78, str, &font_time_24x48, ST7735_CYAN, ST7735_BLACK);

        /* esp32从心知天气获取本地天气数据，返回json结果 */
        const char *rsp;
        bool weather_get_ok = esp_at_get_http(weather_uri, &rsp, NULL, 10000);

        // ✅请求成功才往下执行！失败直接跳过，防止空指针死机
        if (weather_get_ok)
        {
            /* 解析json，获取天气数据 */
            weather_t weather;
            weather_parse(rsp, &weather);
            /* 根据天气内容，选择不同的图片显示 */
            const st_image_t *img = NULL;
            if (strcmp(weather.weather, "Cloudy") == 0)
            {
                img = &icon_weather_duoyun;
            }
            else if (strcmp(weather.weather, "Wind") == 0)
            {
                img = &icon_weather_feng;
            }
            else if (strcmp(weather.weather, "Clear") == 0)
            {
                img = &icon_weather_qing;
            }
            else if (strcmp(weather.weather, "Snow") == 0)
            {
                img = &icon_weather_xue;
            }
            else if (strcmp(weather.weather, "Overcast") == 0)
            {
                img = &icon_weather_yin;
            }
            else if (strcmp(weather.weather, "Rain") == 0)
            {
                img = &icon_weather_yu;
            }

            char str[32]; // 注意！snprintf使用的str要定义！

            if (img != NULL)
            { /* 如果有匹配的天气，则显示天气图标 */
                st7735_draw_image(0, 16, img->width, img->height, img->data);
                st7735_write_string(0, 64, weather.weather, &font_ascii_8x16, ST7735_YELLOW, ST7735_BLACK);
                st7735_write_string(80, 48, "wuhan", &font_ascii_8x16, ST7735_GREEN, ST7735_BLACK);
            }
            else
            { /* 否则直接显示天气文字 */
                snprintf(str, sizeof(str), "%s", weather.weather);
                st7735_write_string(0, 16, str, &font_ascii_8x16, ST7735_YELLOW, ST7735_BLACK);
            }
            /* 显示天气温度 */
            snprintf(str, sizeof(str), "%sC", weather.temperature);
            st7735_write_string(78, 0, str, &font_temper_16x32, ST7735_BLUE, ST7735_BLACK);

            st7735_write_string(0, 127, "happy today", &font_ascii_8x16, ST7735_WHITE, ST7735_BLACK);

            /* 环境温度从mpu6050读取 ^_^ */
            float temper = mpu6050_read_temper();
            snprintf(str, sizeof(str), "%5.1fC", temper);
            st7735_write_string(78, 32, str, &font_ascii_8x16, ST7735_GREEN, ST7735_BLACK);
        }
    }

    // 每100ms更新一次时间
    if (get_tick() % 100 == 0)
    {
        /* 从STM32F4的RTC模块读取年月日时分秒 */
        rtc_date_t date;
        rtc_get_date(&date);
        /* 样例：2024年10月18日则显示：24-10-18 */
        snprintf(str, sizeof(str), "%02d-%02d-%02d", date.year % 100, date.month, date.day);
        st7735_write_string(0, 0, str, &font_ascii_8x16, ST7735_WHITE, ST7735_BLACK);
        /* 样例：12:34，并且中间的":"会1s闪烁一次 */
        snprintf(str, sizeof(str), "%02d%s%02d", date.hour, date.second % 2 ? " " : ":", date.minute);
        st7735_write_string(0, 78, str, &font_time_24x48, ST7735_CYAN, ST7735_BLACK);
    }
    // 每1h联网更新一次时间
    if (get_tick() % (60 * 60 * 1000) == 0)
    {

        /* esp32从sntp服务器读取当前时间，并校准stm32f4的rtc时间 */
        uint32_t ts;
        bool sntp_ok = esp_at_get_time(&ts); // 在这里定义bool变量
        if (sntp_ok)                         // 只有获取时间成功，才设置RTC
        {
            rtc_set_timestamp(ts + 8 * 60 * 60); // +8小时 东八区
        }
    }
    // 每10分钟联网更新一次天气
    if (get_tick() % (10 * 60 * 1000) == 0)
    {
        /* esp32从心知天气获取本地天气数据，返回json结果 */
        const char *rsp;
        bool weather_get_ok = esp_at_get_http(weather_uri, &rsp, NULL, 10000);

        // ✅请求成功才往下执行！失败直接跳过，防止空指针死机
        if (weather_get_ok)
        {
            /* 解析json，获取天气数据 */
            weather_t weather;
            weather_parse(rsp, &weather);
            /* 根据天气内容，选择不同的图片显示 */
            const st_image_t *img = NULL;
            if (strcmp(weather.weather, "Cloudy") == 0)
            {
                img = &icon_weather_duoyun;
            }
            else if (strcmp(weather.weather, "Wind") == 0)
            {
                img = &icon_weather_feng;
            }
            else if (strcmp(weather.weather, "Clear") == 0)
            {
                img = &icon_weather_qing;
            }
            else if (strcmp(weather.weather, "Snow") == 0)
            {
                img = &icon_weather_xue;
            }
            else if (strcmp(weather.weather, "Overcast") == 0)
            {
                img = &icon_weather_yin;
            }
            else if (strcmp(weather.weather, "Rain") == 0)
            {
                img = &icon_weather_yu;
            }

            char str[32]; // 注意！snprintf使用的str要定义！

            if (img != NULL)
            { /* 如果有匹配的天气，则显示天气图标 */
                st7735_draw_image(0, 16, img->width, img->height, img->data);
            }
            else
            { /* 否则直接显示天气文字 */
                snprintf(str, sizeof(str), "%s", weather.weather);
                st7735_write_string(0, 16, str, &font_ascii_8x16, ST7735_YELLOW, ST7735_BLACK);
            }
            /* 显示天气温度 */
            snprintf(str, sizeof(str), "%sC", weather.temperature);
            st7735_write_string(78, 0, str, &font_temper_16x32, ST7735_BLUE, ST7735_BLACK);
        }
    }

    if (get_tick() % (1 * 1000) == 0)
    {
        /* 环境温度从mpu6050读取 ^_^ */
        float temper = mpu6050_read_temper();
        snprintf(str, sizeof(str), "%5.1fC", temper);
        st7735_write_string(78, 32, str, &font_ascii_8x16, ST7735_GREEN, ST7735_BLACK);
    }
}
/**
 * @brief 主程序入口
 *
 * @return main程序不应返回
 */
int main(void)
{
    // 放在main函数最开头
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    tim3_init();

    board_lowlevel_init();

    led_init();
    rtc_init();
    st7735_init();
    mpu6050_init();

    st7735_fill_screen(ST7735_BLACK);

    /* ------------------- 开机显示 ------------------- */
    st7735_write_string(0, 0, "Initializing...", &font_ascii_8x16, ST7735_WHITE, ST7735_BLACK);
    disp_height += font_ascii_8x16.height;
    Delay_ms(1000);

    st7735_write_string(0, disp_height, "Wait ESP32...", &font_ascii_8x16, ST7735_WHITE, ST7735_BLACK);
    disp_height += font_ascii_8x16.height;
    Delay_ms(1500);

    wifi_init();
    st7735_write_string(0, disp_height, "OK", &font_ascii_8x16, ST7735_WHITE, ST7735_BLACK);
    disp_height += font_ascii_8x16.height;
    Delay_ms(500);

    st7735_fill_screen(ST7735_BLACK);

    uint32_t i = 0;
    char str[32];

    while (1)
    {
        // sprintf(str, "%04u", i++);
        // i = i % 10000;
        // st7735_write_string(0, 0, str, &font_ascii_8x16, ST7735_WHITE, ST7735_BLACK);
        // st7735_write_string(0, 18, str, &font_temper_16x32, ST7735_WHITE, ST7735_BLACK);
        // st7735_write_string(0, 52, str, &font_time_24x48, ST7735_WHITE, ST7735_BLACK);
        // st7735_write_string(0, 102, "hello world", &font_ascii_8x16, ST7735_WHITE, ST7735_BLACK);

        // Delay_ms(1000);
        update();
    }
}
