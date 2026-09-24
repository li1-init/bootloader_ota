/**
  ******************************************************************************
  1. A区 存放程序
  2. B区 存放bootloader
  ******************************************************************************
  */

#ifndef __BOOTLOADER_H
#define __BOOTLOADER_H

#include "stm32f10x.h"
#include "string.h"
#include "stdio.h"


/* ------------------- stm32flash分区宏定义 ------------------- */
/**
 * @brief 定义bootloader的地址
 *
 */
#define stm32_flash_saddr 0x08000000u // FLASH起始地址
#define stm32_page_size 2048u         // STM32‑F1每页大小：2048字节！！
#define stm32_page_num 64u            // 总页数，128KB / 2048 = 64页

#define stm32_b_page_num 8u // B区页数，8页

#define stm32_a_page_num (stm32_page_num - stm32_b_page_num)                     // A区页数，56页
#define stm32_a_start_page stm32_b_page_num                                      // A区起始页，第8页
#define stm32_a_saddr (stm32_flash_saddr + stm32_a_start_page * stm32_page_size) // A区起始地址  //A区起始地址：0x08000000 + 8*2048 = 0x08004000

// 可选补充边界，用于ota烧录时判断地址越界
#define stm32_b_eaddr (stm32_flash_saddr + stm32_b_page_num * stm32_page_size - 1u)
#define stm32_a_eaddr (stm32_a_saddr + stm32_a_page_num * stm32_page_size - 1u)

// sram范围 stm32f103c8t6
#define sram_begin 0x20000000u
#define sram_end 0x20004fffu


/**
 * @brief 定义OTA相关的标志位
 *
 */
#define ota_flag_set 0xAABB1122u
#define OTA_W25Q_ADDR 0x000000U // W25Q64存放标志的地址
#define ota_magic        0x55AA5A5AU  //魔数的标志

typedef __packed struct
{
    uint32_t magic;          //魔数，固定 0x55AA5A5A
    uint32_t OTA_flag;       //升级标志 ota_flag_set=0xAABB1122u
    uint32_t fw_len;         //新固件字节长度
    uint32_t fw_crc;         //新固件CRC32校验值
    uint8_t  version[16];    //固件版本字符串
    uint32_t crc;            //前面所有数据的校验和;
} OTA_t;

#define ota_t_size sizeof(OTA_t)








/* ------------------- w25q64 地址规划 ------------------- */
#define w25q_ota_info_addr       0x000000u       //ota标志信息结构体存放地址
#define w25q_fw_store_start      0x010000u       //固件临时缓存起始地址(全部下载到这里)

#define fw1_addr 0x010000u
#define fw2_addr 0x020000u
#define fw3_addr 0x030000u
#define fw4_addr 0x040000u
#define fw5_addr 0x050000u
#define fw6_addr 0x060000u
#define fw7_addr 0x070000u
#define fw8_addr 0x080000u
#define fw9_addr 0x090000u
#define fw_one_size 0x10000u

/* ------------------- 标志位掩码定义(位标志) ------------------- */
#define update_a_flag             (1u << 0)      //bit0：标记升级a分区

/* ------------------- 分片下载参数 ------------------- */
#define packet_size               256u            //每一包分片大小，匹配w25q页写入上限

/* ------------------- ota信息结构体，存放到w25q64 ------------------- */
typedef struct
{
    uint32_t magic;                 //魔数 0x55aa5a5a，用来校验数据是否有效
    uint32_t boot_sta_flag;         //ota状态标志位，位掩码
    uint32_t fw_total_len;          //固件总字节长度
    uint32_t fw_crc32;              //固件校验值
    uint32_t block_nb;              //已经接收完成的分片包数量
    char version[32];               // 固件版本号，字符串
    uint32_t fw_len[9];             // 9个外部Flash固件槽的长度             //固件版本号，字符串
} __attribute__((packed)) ota_info_typedef;

#define ota_info_typedef_size sizeof(ota_info_typedef)
#define  magic_flag 0x55aa5a5a

/* ------------------- 下载控制结构体------------------- */
typedef struct
{
    uint8_t rx_buf[packet_size];    //256字节分片接收缓冲区，ram只开一小块缓存
    uint32_t block_cnt;             //当前收到第几包分片
    uint8_t rx_done_flag;           //单包接收完成标志
} update_ctrl_typedef;


/* ------------------- 函数声明 ------------------- */

void ota_flag_test(void);
void jump_to_app(void);

void ota_download_init(void);
void set_ota_update_flag(uint32_t fw_len, uint32_t crc_val);
void write_one_packet_to_w25q(uint8_t *buf, uint32_t block_index);
void finish_download_reset(uint32_t fw_len, uint32_t crc);
void read_ota_info_from_w25q(void);
uint8_t check_need_update_a(void);
void copy_fw_from_w25q_to_internal_flash(void);
void clear_update_a_flag(void);
void boot_main_process(void);
uint32_t crc32_calc(uint8_t *buf, uint32_t len);
void test_set_ota_flag(void);

/* ------------------- 变量声明 ------------------- */

extern OTA_t OTA_Info;

extern ota_info_typedef ota_info;

#endif
