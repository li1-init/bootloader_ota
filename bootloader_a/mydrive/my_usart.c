#include "stm32f10x.h" // Device header
#include "my_usart.h"
#include "W25Q64.h"
#include "MySPI.h"
/**
 * @brief 串口接收缓冲区
 *
 */
uint8_t usart_rx_buf[usart_rx_buf_size];

usart_data U0CB;

void UORx_PtrInit(void)
{
    U0CB.URxDataIN = &U0CB.URxDataPtr[0];  // 与DMA+空闲中断写入相关
    U0CB.URxDataOUT = &U0CB.URxDataPtr[0]; // 与读取相关
    U0CB.URxDataEND = &U0CB.URxDataPtr[num - 1];
    U0CB.URxDataIN->start = usart_rx_buf;
    U0CB.URxCounter = 0;
}

void my_usart1_init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
    USART_InitTypeDef usart_struct = {0};
    usart_struct.USART_BaudRate = 115200;
    usart_struct.USART_HardwareFlowControl = DISABLE;
    usart_struct.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    usart_struct.USART_Parity = USART_Parity_No;
    usart_struct.USART_StopBits = USART_StopBits_1;
    usart_struct.USART_WordLength = USART_WordLength_8b;
    USART_Init(USART1, &usart_struct);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitTypeDef DPA = {0};
    DPA.GPIO_Pin = GPIO_Pin_9;
    DPA.GPIO_Mode = GPIO_Mode_AF_PP;
    DPA.GPIO_Speed = GPIO_Speed_10MHz;
    GPIO_Init(GPIOA, &DPA);
    DPA.GPIO_Mode = GPIO_Mode_IPU;
    DPA.GPIO_Pin = GPIO_Pin_10;
    GPIO_Init(GPIOA, &DPA);

    //=====================【新增开始：DMA+空闲中断】=====================

    // 1.开启DMA1时钟，DMA1挂载在AHB总线
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    // 2.定义DMA初始化结构体
    DMA_InitTypeDef dma_rx_struct = {0};

    // 外设地址：USART1的数据寄存器地址，串口收到字节就往这里放
    dma_rx_struct.DMA_PeripheralBaseAddr = (uint32_t)&USART1->DR;

    // 内存目标地址：你自己定义的接收大缓冲区数组，例如 uint8_t rx_dma_buf[2048];
    dma_rx_struct.DMA_MemoryBaseAddr = (uint32_t)usart_rx_buf;

    // 传输方向：外设 → 内存 (串口收到数据→存到数组)
    dma_rx_struct.DMA_DIR = DMA_DIR_PeripheralSRC;

    // DMA缓存大小，和你的数组长度一致
    dma_rx_struct.DMA_BufferSize = usart_rx_buf_max + 1;

    // 外设地址：不递增，串口寄存器地址永远不变
    dma_rx_struct.DMA_PeripheralInc = DMA_PeripheralInc_Disable;

    // 内存地址开启自增：收到下一字节存到数组下一格
    dma_rx_struct.DMA_MemoryInc = DMA_MemoryInc_Enable;

    // 外设数据宽度：1字节
    dma_rx_struct.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;

    // 内存数据宽度：1字节
    dma_rx_struct.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;

    // DMA模式：普通模式，串口DMA接收标配
    dma_rx_struct.DMA_Mode = DMA_Mode_Normal;

    // 通道优先级：中等
    dma_rx_struct.DMA_Priority = DMA_Priority_Medium;

    // 不开启内存到内存
    dma_rx_struct.DMA_M2M = DMA_M2M_Disable;

    // 3.把所有配置写入DMA1通道5（USART1_RX固定通道）
    DMA_Init(DMA1_Channel5, &dma_rx_struct);

    // 4.串口接收使用DMA模式，告诉硬件：收到字节交给DMA搬运，不用CPU干预
    USART_DMACmd(USART1, USART_DMAReq_Rx, ENABLE);

    // 5.使能DMA1通道5，DMA开始干活等待接收数据
    DMA_Cmd(DMA1_Channel5, ENABLE);

    // -----------串口空闲中断配置（收到一帧之后总线空闲就进中断）-----------

    // 开启串口空闲线中断 IDLE
    USART_ITConfig(USART1, USART_IT_IDLE, ENABLE);

    // NVIC中断分组配置，全局只配置一次，放main最前面即可，不要重复放这里
    // NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    // 6.配置串口1中断优先级
    NVIC_InitTypeDef nvic_struct = {0};
    nvic_struct.NVIC_IRQChannel = USART1_IRQn;
    nvic_struct.NVIC_IRQChannelPreemptionPriority = 1;
    nvic_struct.NVIC_IRQChannelSubPriority = 1;
    nvic_struct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic_struct);

    //=====================【新增结束】=====================
    UORx_PtrInit();

    USART_Cmd(USART1, ENABLE);
}

void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_IDLE) != RESET)
    {
        //======== STM32F1清除IDLE标志，必须读SR再读DR ========
        (void)USART_GetFlagStatus(USART1, USART_FLAG_IDLE);
        (void)USART_ReceiveData(USART1);

        // DMA剩余未传输字节数
        uint16_t remain = DMA_GetCurrDataCounter(DMA1_Channel5);
        // 当前收到这一帧数据的长度
        uint16_t recv_len = (usart_rx_buf_max + 1) - remain;

        /*
         这里写你的代码：
         把 rx_dma_buf[0 ~ recv_len‑1] 的这一帧数据送入字节环形缓冲区
        */
        if (recv_len == 0)
        {
            DMA_Cmd(DMA1_Channel5, DISABLE);
            DMA_SetCurrDataCounter(DMA1_Channel5, usart_rx_buf_max + 1);
            DMA1_Channel5->CMAR = (uint32_t)U0CB.URxDataIN->start;
            DMA_Cmd(DMA1_Channel5, ENABLE);
            return;
        }
        U0CB.URxCounter = U0CB.URxCounter + recv_len;             // 计算当前接收数据长度
        U0CB.URxDataIN->end = &usart_rx_buf[U0CB.URxCounter - 1]; // 把写入结束位置找到
        U0CB.URxDataIN++;                                         // URxDataPtr[num] 相当于一个标签只记住开始和结束在usart_rx_buf[usart_rx_buf_size]的哪里一共有10个盒子第一个盒子记住了第一次传输数据的开始和结束位置，第二个盒子记住了第二次传输数据的开始和结束位置，以此类推。
        if (U0CB.URxDataIN == U0CB.URxDataEND)                    // 如果写入指针指向了最后一个盒子，就重置为第一个盒子  避免指针越界
        {
            U0CB.URxDataIN = &U0CB.URxDataPtr[0];
        }

        if (usart_rx_buf_size - U0CB.URxCounter >= usart_rx_buf_max) // 看usart_rx_buf[usart_rx_buf_size]是否有足够的空间 有的继续写没有的话就重置为第一个盒子
        {
            U0CB.URxDataIN->start = &usart_rx_buf[U0CB.URxCounter];
        }
        else
        {
            U0CB.URxDataIN->start = usart_rx_buf;
            U0CB.URxCounter = 0;
        } // 经过这个if已经把下一次的写入开始位置找出来了 开始通知DMA接收

        // 1.关闭DMA通道
        DMA_Cmd(DMA1_Channel5, DISABLE);

        // 2.设置本次DMA接收的字节数
        DMA_SetCurrDataCounter(DMA1_Channel5, usart_rx_buf_max + 1);

        // 3.修改DMA内存目标地址（对应GD32 dma_memory_address_config）
        DMA1_Channel5->CMAR = (uint32_t)U0CB.URxDataIN->start;

        // 4.重新开启DMA，等待下一包数据
        DMA_Cmd(DMA1_Channel5, ENABLE);
    }
}

uint8_t usart_tx_buf[usart_tx_buf_size];

/**
 * @brief 串口1打印函数
 *
 * @param fmt
 * @param ...
 */
void u1_printf(const char *fmt, ...)
{
    uint16_t i = 0;

    va_list listdata;
    va_start(listdata, fmt);
    vsprintf((char *)usart_tx_buf, fmt, listdata);
    va_end(listdata);

    for (i = 0; i < strlen((char *)usart_tx_buf); i++)
    {
        // 等待发送数据寄存器空 TXE
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET)
            ;
        // 写入1字节到串口发送寄存器
        USART_SendData(USART1, usart_tx_buf[i]);
    }
    // 等待最后一字节完全发送完成
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET)
        ;
    // 手动清除TC标志！非常关键
    USART_ClearFlag(USART1, USART_FLAG_TC);
}

void all_init(void)
{
   
    my_usart1_init();
    Delay_Init();
    W25Q64_Init();
}
