#include "stm32f10x.h" // Device header
#include "struct_init.h"

/* ------------------- 变量声明 ------------------- */
volatile uint32_t now = 0;

/* ------------------- 外设初始化 ------------------- */
void gpio_initstruct(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    GPIO_InitTypeDef gpio_initstruct = {0};

    gpio_initstruct.GPIO_Mode = GPIO_Mode_IPU;
    gpio_initstruct.GPIO_Pin = GPIO_Pin_1;
    GPIO_Init(GPIOA, &gpio_initstruct);

    gpio_initstruct.GPIO_Mode = GPIO_Mode_IPU;
    gpio_initstruct.GPIO_Pin = GPIO_Pin_2;
    GPIO_Init(GPIOA, &gpio_initstruct);

    gpio_initstruct.GPIO_Mode = GPIO_Mode_IPU;
    gpio_initstruct.GPIO_Pin = GPIO_Pin_3;
    GPIO_Init(GPIOA, &gpio_initstruct);

    gpio_initstruct.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio_initstruct.GPIO_Pin = GPIO_Pin_4;
    gpio_initstruct.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOB, &gpio_initstruct);

    gpio_initstruct.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio_initstruct.GPIO_Pin = GPIO_Pin_13;
    gpio_initstruct.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOC, &gpio_initstruct);
    
    
    
    // 设置初始状态
    GPIO_WriteBit(GPIOC, GPIO_Pin_13, Bit_SET);
}

/* ------------------- 定时器3初始化 ------------------- */
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
