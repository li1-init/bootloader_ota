#include "stm32f10x.h"
#include "my_usart.h"
#include "stdio.h"
#include "W25Q64.h"
#include "MySPI.h"
#include "bootloader.h"
#include "stm32f10x_flash.h"
#include "delay.h"
#include "usart_command.h"

/* ------------------- 变量声明 ------------------- */
OTA_t OTA_Info = {0};
ota_info_typedef ota_info = {magic_flag,0,0,0,0,0};


/* ------------------- 跳转B区函数声明 ------------------- */
/**
 * @brief  函数指针类型：app复位入口函数格式，无参无返回
 */
typedef void (*jumpapp_fun)(void);

/**
 * @brief  bootloader跳转到a分区app程序
 * @retval 无
 */
/**
 * @brief 跳转到应用程序主函数
 * @note 此函数实现从bootloader跳转到用户应用程序的功能，完整复刻了芯片冷上电流程
 * @warning 跳转前会进行安全校验，确保应用程序栈地址合法，否则会进入死循环
 */
void jump_to_app(void)
{

    //========步骤1：读取app向量表前两项========
    // 向量表偏移0：app初始栈顶msp
    uint32_t app_msp_value = *(volatile uint32_t *)stm32_a_saddr;
    // 向量表偏移+4：app复位入口reset_handler地址
    uint32_t app_reset_addr = *(volatile uint32_t *)(stm32_a_saddr + 4);

    //========步骤2：安全校验栈地址是否合法========
    // 栈顶必须落在单片机sram区间 0x20000000~0x20004fff
    if (app_msp_value < sram_begin || app_msp_value > sram_end)
    {
        // app固件损坏，栈地址非法，放弃跳转，死循环卡住
       u1_printf("跳转到APP区失败\r\n");
	    menu7();
    }

    //========步骤3：跳转前清理，防止外设、中断残留干扰app========
    __disable_irq(); // 关闭全局中断，跳转期间禁止中断触发

    RCC_DeInit(); // 标准库函数，复位所有外设时钟回到上电默认状态

    // 清空systick滴答定时器，清除boot延时定时器残留
    SysTick->CTRL = 0x00;
    SysTick->LOAD = 0x00;
    SysTick->VAL = 0x00;
    
    __enable_irq();   //开启全局中断


    //========步骤4：复刻芯片冷上电硬件流程========
    __set_MSP(app_msp_value); // 手动设置主栈指针msp，等价于上电硬件读取0地址设置sp

    jumpapp_fun app_reset_handler; // 定义函数指针，保存app复位入口地址

    app_reset_handler = (jumpapp_fun)app_reset_addr; // 复位入口地址赋值给函数指针

    app_reset_handler(); // 跳转，修改pc寄存器，进入app程序，不再返回boot

    // 防护代码，正常永远不会运行到此
    while (1)
        ;
}

/* ------------------- OTA相关函数声明 ------------------- */
void ota_flag_test(void)
{
    uint32_t w25q64_addr = OTA_W25Q_ADDR;

    // 从W25Q64读取标志，把结构体强制转成uint8_t字节指针，读取4字节
    W25Q64_ReadData(w25q64_addr, (uint8_t *)&OTA_Info, ota_t_size);

    if (OTA_Info.OTA_flag == ota_flag_set && OTA_Info.magic == ota_magic)
    {
        u1_printf("OTA更新\r\n");
        u1_printf("w25q64_flag:0x%08X\r\n", OTA_Info.OTA_flag);
    }
    else
    {
        u1_printf("跳转到APP区\r\n");
        u1_printf("w25q64_flag:0x%08X\r\n", OTA_Info.OTA_flag);
        jump_to_app();
    }
}


update_ctrl_typedef update_ctrl;    //创建下载控制全局变量
ota_info_typedef ota_info;          //ota信息参数缓存

/**
  * @brief  初始化ota下载，准备接收新固件
  */
void ota_download_init(void)
{
    uint32_t i;
    for(i = 0; i < packet_size; i++)
    {
        update_ctrl.rx_buf[i] = 0;  //清空接收缓冲区
    }
    update_ctrl.block_cnt = 0;      //分片计数器清零，从第0包开始下载
    update_ctrl.rx_done_flag = 0;  //接收完成标志清零
}

/**
  * @brief  保存ota升级标志，写入外部w25q64
  */
void set_ota_update_flag(uint32_t fw_len,uint32_t crc_val)
{
    ota_info.magic = 0x55aa5a5a;                //写入魔数，用来boot判断数据合法
    ota_info.boot_sta_flag |= update_a_flag;    //【重点】位或！开启升级a分区标记，保留其他旧标志
    ota_info.fw_total_len = fw_len;             //固件总长度保存
    ota_info.fw_crc32 = crc_val;                //固件crc校验码
    ota_info.block_nb = update_ctrl.block_cnt;  //已经接收包的数量

    W25Q64_SectorErase(w25q_ota_info_addr);    //擦除w25q存放ota信息的扇区
    W25Q64_PageProgram(w25q_ota_info_addr,(uint8_t *)&ota_info,sizeof(ota_info_typedef));
    //把整个结构体写入w25q64
}

/* ------------------- W25Q64写入写出函数声明 ------------------- */
/**
 * @brief  分片一包数据写入w25q固件缓存区
 * @param  buf:一包256字节数据缓存首地址
 * @param  block_index:当前分片包序号
 */
void write_one_packet_to_w25q(uint8_t *buf,uint32_t block_index)
{
    uint32_t write_addr;
    //计算这一包数据在w25q64里面的存放地址：起始地址 + 包序号*256字节
    write_addr = w25q_fw_store_start + block_index * packet_size;
    W25Q64_PageProgram(write_addr, buf, packet_size); //执行页写入
    update_ctrl.block_cnt ++;    //分片计数+1，记录成功写入一包
}

/**
  * @brief  全部固件下载完成，复位跳去bootloader执行升级
  */
void finish_download_reset(uint32_t fw_len,uint32_t crc)
{
    set_ota_update_flag(fw_len,crc);    //写入升级标记到w25q64
    NVIC_SystemReset();			//软件复位单片机，重启进入bootloader
}

/**
  * @brief  从w25q64读取ota状态信息
  */
void read_ota_info_from_w25q(void)
{
    W25Q64_ReadData(w25q_ota_info_addr,(uint8_t *)&ota_info,sizeof(ota_info_typedef));
}

/**
  * @brief  判断是否需要升级a分区
  * @retval 1:升级； 0:不需要升级
  */
uint8_t check_need_update_a(void)
{
    //先校验魔数是否正确，防止w25q里面是随机垃圾数据
    if(ota_info.magic != 0x55aa5a5a)
    {
        return 0;   //魔数不对，无效升级信息
    }
    //&按位与，检查update_a_flag这一位有没有被置1
    if((ota_info.boot_sta_flag & update_a_flag) != 0 )
    {
        return 1;   //检测到a分区升级标记
    }
    return 0;
}



/**
  * @brief  将w25q64存放的固件搬运烧写到单片机内部flash a分区
  */
void copy_fw_from_w25q_to_internal_flash(void)
{
    uint32_t internal_addr;        //单片机内部flash写入地址
    uint32_t w25q_read_addr;       //w25q读取源地址
    uint8_t temp_buf[packet_size];  //ram临时缓存256字节
    uint32_t remain_len;            //剩余还需要搬运的字节数
    uint32_t i;

    internal_addr = stm32_a_saddr;       //内部flash目标地址：a分区起始
    w25q_read_addr = w25q_fw_store_start;  //w25q固件缓存起始地址
    remain_len = ota_info.fw_total_len;     //剩余字节初始化为固件全长

    FLASH_Unlock();                         //解锁stm32内部flash，允许擦除写入

    //循环：每次搬运256字节一包
    while(remain_len > 0)
    {
        //从w25q读出一包256字节固件
        W25Q64_ReadData(w25q_read_addr, temp_buf, packet_size);

        //每2048字节(1页)先擦除单片机flash页，f1必须先擦后写
        if((internal_addr - stm32_a_saddr) % stm32_page_size == 0)
        {
            FLASH_ErasePage(internal_addr);
        }

        //循环写入这256字节，每次写入2字节(半字，stm32f1要求)
        for(i = 0; i < packet_size; i += 2)
        {
            uint16_t half_word;
            half_word = (temp_buf[i+1] << 8) | temp_buf[i]; //高低字节拼成16位半字
            FLASH_ProgramHalfWord(internal_addr + i, half_word);
        }

        //地址偏移
        internal_addr += packet_size;
        w25q_read_addr += packet_size;

        if(remain_len >= packet_size)
        {
            remain_len -= packet_size;
        }
        else
        {
            remain_len = 0; //最后不足256字节时结束循环
        }
    }

    FLASH_Lock();   //写完锁定内部flash
}

/**
  * @brief  清除a分区升级标志，升级完成
  */
void clear_update_a_flag(void)
{
    //&= ~  清除指定bit，其他标志位保留不动
    ota_info.boot_sta_flag &= ~update_a_flag;
    W25Q64_SectorErase(w25q_ota_info_addr);
    W25Q64_PageProgram(w25q_ota_info_addr,(uint8_t *)&ota_info,sizeof(ota_info_typedef));
}

/**
 * @brief 标准CRC32‑IEEE校验
 * @param  buf:数据起始地址
 * @param  len:校验字节长度
 * @retval crc32结果
 */
uint32_t crc32_calc(uint8_t *buf, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    uint32_t i,j;
    for(i = 0; i < len; i++)
    {
        crc ^= buf[i];
        for(j = 0; j < 8; j++)
        {
            if(crc & 1)
            {
                crc = (crc >> 1) ^ 0xEDB88320U;
            }
            else
            {
                crc = crc >> 1;
            }
        }
    }
    return ~crc;
}

/* ------------------- bootloader函数声明 ------------------- */
/**
 * @brief  boot主逻辑，放在boot的main函数里面循环执行
 */
void boot_main_process(void)
{
      uint32_t calc_crc;
	
	/* ------------------- 测试CRC校验（恢复时记得调用W25Q64_SectorErase擦除） ------------------- */
	
//    ota_info_typedef ota_info;

//    ota_info.magic        = 0x55AA5A5A;      //魔数合法
//    ota_info.boot_sta_flag |= update_a_flag; //开启A区升级标志
//    ota_info.fw_total_len =  1024;           //假装固件长度1024字节
//    ota_info.fw_crc32     =  0x12345678;     //假装预期CRC值
//    ota_info.block_nb     =  4;

      W25Q64_SectorErase(w25q_ota_info_addr);
//    W25Q64_PageProgram(w25q_ota_info_addr,(uint8_t *)&ota_info,sizeof(ota_info_typedef));
	
	
	
	
    read_ota_info_from_w25q();         //第一步读取ota配置信息

	
	
    if(check_need_update_a() == 1)     //第二步判断是否升级
    {
        printf("\r\n进行A区升级\r\n");
        copy_fw_from_w25q_to_internal_flash(); //搬运固件从w25q写入单片机flash

        //=====新增：对刚烧写到内部Flash的固件做CRC校验=====
        calc_crc = crc32_calc((uint8_t *)stm32_a_saddr, ota_info.fw_total_len);

        if(calc_crc == ota_info.fw_crc32)
        {
            printf("\r\n固件CRC校验成功！\r\n");
            clear_update_a_flag();               //升级成功清除升级标记
        }
        else
        {
            printf("\r\n固件CRC校验失败！放弃升级，保留升级标记，2秒后重启\r\n");
            //校验失败！！不清除OTA标志，下次重启boot会重新执行升级
            while(1)
		{
			
		}
		  
        }
    }
    printf("\r\n跳转APP区\r\n");
    printf("/* -------------------------------------- */\r\n");
    printf("\r\n");

    jump_to_app();                         //跳转app运行
}





