#include "stm32f10x.h"
#include "my_usart.h"
#include "stdio.h"
#include "W25Q64.h"
#include "MySPI.h"
#include "bootloader.h"
#include "stm32f10x_flash.h"
#include "delay.h"
#include "usart_command.h"
#include <stdarg.h>
#include "struct_init.h"
#include "stdint.h"
#include "esp_at.h"
#define STM32_FLASH_ERASE_PAGE_SIZE 1024u
#define copy_buf_len 256u
uint8_t copy_buf[copy_buf_len]; // 定义一块 SRAM 数组（缓冲区）

extern uint8_t mqtt_line_ready;	 // 1代表mqtt_line_buf已经存好完整一行文本
extern uint8_t g_mqtt_msg_ready; // 1：代表成功解析出来一条MQTT上报消息

extern char mqtt_line_buf[MQTT_LINE_BUF_LEN];

extern MqttSubRecv_t g_mqtt_sub_msg;
extern uint8_t g_mqtt_msg_ready;

/**
 * @brief 解析 +MQTTSUBRECV:0,"stm32/cmd",6,123456
 */
void mqtt_parse_sub_recv(char *line)
{
	g_mqtt_msg_ready = 0;

	// 判断是不是MQTT上报行，如果不是直接返回
	if (strstr(line, "+MQTTSUBRECV:") == NULL)
	{
		return;
	}

	// 跳过 "+MQTTSUBRECV:"
	char *p = strstr(line, ":") + 1;

	// 跳过client编号 0,
	p = strchr(p, ',') + 1;
	if (p == NULL)
		return;

	p++; // 跳过第一个双引号
	char *topic_end = strchr(p, '"');
	if (topic_end == NULL)
		return;

	// 计算主题长度，拷贝，不修改原line缓冲区
	size_t topic_len = topic_end - p;
	strncpy(g_mqtt_sub_msg.topic, p, sizeof(g_mqtt_sub_msg.topic) - 1);
	g_mqtt_sub_msg.topic[topic_len] = '\0';

	p = topic_end + 1;

	// 跳过 ,6,
	p = strchr(p, ',') + 1;
	if (p == NULL)
		return;
	p = strchr(p, ',') + 1;
	if (p == NULL)
		return;

	// 复制payload，清除\r\n回车换行
	strncpy(g_mqtt_sub_msg.payload, p, sizeof(g_mqtt_sub_msg.payload) - 1);
	g_mqtt_sub_msg.payload[strcspn(g_mqtt_sub_msg.payload, "\r\n")] = '\0';

	// 标记：解析成功，g_mqtt_sub_msg内topic、payload数据有效
	g_mqtt_msg_ready = 1;
}

// 只提取0‑9数字，返回uint8_t；无有效数字返回 0xFF
uint8_t get_only_number(uint8_t *buf, uint16_t len)
{
	uint8_t num_1 = 0;
	unsigned char have_digit = 0;
	unsigned int i;
	for (i = 0; i < len; i++)
	{
		if (buf[i] >= '0' && buf[i] <= '9')
		{
			num_1 = num_1 * 10 + (buf[i] - '0');
			have_digit = 1;
		}
	}
	if (have_digit)
	{
		return num_1;
	}
	// 没有找到任何数字，返回0xFF作为无效标记
	return 0xFF;
}

static void stm32_program_flash(uint32_t addr, const uint8_t *pbuf, uint16_t len)
{
	uint16_t i;

	FLASH_Unlock();
	FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);

	for (i = 0; i < len; i += 2)
	{
		uint16_t half_word = (uint16_t)pbuf[i];
		if ((uint16_t)(i + 1u) < len)
		{
			half_word |= (uint16_t)pbuf[i + 1u] << 8;
		}
		else
		{
			half_word |= 0xFF00u;
		}
		FLASH_ProgramHalfWord(addr + i, half_word);
	}

	FLASH_Lock();
}

uint8_t copy_w25q_to_app(uint8_t fw_no, uint32_t fw_len)
{
	uint32_t w25_base;
	uint32_t offset;
	uint16_t read_len;

	switch (fw_no)
	{
	case 1:
		w25_base = fw1_addr;
		break;
	case 2:
		w25_base = fw2_addr;
		break;
	case 3:
		w25_base = fw3_addr;
		break;
	case 4:
		w25_base = fw4_addr;
		break;
	case 5:
		w25_base = fw5_addr;
		break;
	case 6:
		w25_base = fw6_addr;
		break;
	case 7:
		w25_base = fw7_addr;
		break;
	case 8:
		w25_base = fw8_addr;
		break;
	case 9:
		w25_base = fw9_addr;
		break;
	default:
		return 1;
	}

	if (fw_len > (stm32_a_page_num * stm32_page_size))
	{
		u1_printf("固件长度超出分区\r\n");
		return 2;
	}

	__disable_irq();

	// -------------------- 重点！！先整片擦除APP分区 --------------------
	STM32_EraseFlash(stm32_a_start_page, stm32_a_page_num);

	// 擦除完成后逐块写入
	for (offset = 0; offset < fw_len; offset += 256)
	{
		if ((fw_len - offset) < 256)
		{
			read_len = fw_len - offset;
		}
		else
		{
			read_len = 256;
		}
		W25Q64_ReadData(w25_base + offset, copy_buf, read_len);
		if (read_len & 1u)
		{
			copy_buf[read_len] = 0xFF;
			read_len++;
		}
		stm32_program_flash(stm32_a_saddr + offset, copy_buf, read_len);
	}

	uint8_t flash_buf[256];
	uint32_t verify_err = 0xFFFFFFFFu;
	for (offset = 0; offset < fw_len; offset += 256)
	{
		uint16_t len = ((fw_len - offset) < 256) ? (fw_len - offset) : 256;
		W25Q64_ReadData(w25_base + offset, copy_buf, len);
		memcpy(flash_buf, (void *)(stm32_a_saddr + offset), len);
		if (memcmp(copy_buf, flash_buf, len) != 0)
		{
			verify_err = offset;
			break;
		}
	}

	__enable_irq();

	if (verify_err != 0xFFFFFFFFu)
	{
		u1_printf("搬运校验失败！出错偏移地址:0x%08X\r\n", verify_err);
		return 3;
	}
	else
	{
		u1_printf("搬运校验成功！数据完全一致\r\n");
		return 0;
	}
}
/**
 * @brief  循环擦除W25Q64一块64KB空间（16个4KB扇区）
 * @param  base_addr: 分区起始地址，例如 fw1_addr = 0x010000u
 * @retval 无
 */
void w25q_erase_64k_block(uint32_t base_addr)
{
	uint16_t i;
	// 64KB / 4KB = 16个扇区
	for (i = 0; i < 16; i++)
	{
		W25Q64_SectorErase(base_addr + i * 4096u);
	}
}
void tell(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\r\n");
	printf("/* -------------------------------------- */\r\n");
}

/* ------------------- bootloader菜单 ------------------- */
void uasrt_command_suf(void)
{
	u1_printf("\r\n");
	u1_printf("\r\n");
	u1_printf("bootloader命令行\r\n");
	u1_printf("/* -------------------------------------- */\r\n");
	u1_printf("[1]擦除A区\r\n");
	u1_printf("[2]串口IAP下载A区程序\r\n");
	u1_printf("[3]设置OTA版本号\r\n");
	u1_printf("[4]查询OTA版本号\r\n");
	u1_printf("[5]向外部Flash下载程序\r\n");
	u1_printf("[6]使用外部Flash内程序\r\n");
	u1_printf("[7]重启\r\n");
	u1_printf("[8]跳转进入A区\r\n");
	u1_printf("[9]远程接收消息\r\n");
	u1_printf("/* -------------------------------------- */\r\n");
}

extern volatile uint32_t now;
extern uint8_t usart_rx_buf[usart_rx_buf_size];

uint8_t uasrt_command(uint32_t time)
{
	uint32_t start_ms = now;
	u1_printf("\r\n请在%2d秒内输入 “w” 进入命令行，否则自动进入APP区\r\n", time);

	while (now - start_ms < time * 1000)
	{
		if (usart_rx_buf[0] == 'w')
		{
			return 1;
		}
	}
	return 0;
}

void uasrt_command_test(void)
{
	uint8_t return_val = 0;
	return_val = uasrt_command(3);

	if (return_val == 1)
	{
		uasrt_command_suf();
	}
	if (return_val == 0)
	{
		u1_printf("跳转到APP区\r\n");
		jump_to_app();
	}
}

/**
 * @brief  擦除Flash多个页面
 * @param  start_page:起始页号(F1每页2048字节)
 * @param  page_cnt:擦除页数
 */
void STM32_EraseFlash(uint32_t start_page, uint32_t page_cnt)
{
    uint32_t i;
    uint32_t base_addr;
    uint32_t total_bytes;
    uint32_t erase_count;

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);

    /*
     * start_page 仍然按原来的 2KB 逻辑计算起始地址。
     * 但 F103C8T6 实际擦除页是 1KB，所以这里按 1KB 全部擦一遍。
     */
    base_addr = 0x08000000u + start_page * 2048u;
    total_bytes = page_cnt * 2048u;
    erase_count = total_bytes / STM32_FLASH_ERASE_PAGE_SIZE;

    for (i = 0; i < erase_count; i++)
    {
        FLASH_ErasePage(base_addr + i * STM32_FLASH_ERASE_PAGE_SIZE);
    }

    FLASH_Lock();
}

/* ------------------- 校验 XmodemCRC16 每一包函数 ------------------- */
/**
 * @brief  Xmodem CRC16‑CCITT 校验函数
 * @param  data:数据缓冲区指针
 * @param  datalen:需要校验的数据长度,Xmodem一包填128
 * @retval 计算完成的16位CRC值
 */
uint16_t Xmodem_CRC16(uint8_t *data, uint16_t datalen)
{
	uint8_t i;
	uint16_t Crcinit = 0x0000;
	uint16_t Crcipoly = 0x1021;

	while (datalen--)
	{
		Crcinit = (*data << 8) ^ Crcinit;
		for (i = 0; i < 8; i++)
		{
			if (Crcinit & 0x8000)
			{
				Crcinit = (Crcinit << 1) ^ Crcipoly;
			}
			else
			{
				Crcinit = (Crcinit << 1);
			}
		}
		data++;
	}
	return Crcinit;
}

void crc16_test(void)
{
	uint8_t aaa[5] = {1, 2, 3, 4, 5};
	printf("%x\r\n", Xmodem_CRC16(aaa, 5));
	while (1)
		;
}

/* ------------------- 串口下载处理函数 ------------------- */

typedef struct
{
	uint16_t XmodemNB;					 // 接收包计数器
	uint16_t XmodemCRC;					 // 本地计算CRC
	uint8_t Updatabuff[stm32_page_size]; // RAM页缓存 2048字节
} UpDataA_t;
UpDataA_t UpDataA;
extern uint32_t BootStaFlag;

/**
 * @brief  往STM32F103内部Flash写入一页数据
 * @param  addr: 写入目标地址，必须2字节对齐
 * @param  pbuf: 数据源缓存指针
 * @param  len : 需要写入的字节数(2048以内，偶数)
 * @retval 无
 */
void stm32_write_flash(uint32_t addr, uint8_t *pbuf, uint16_t len)
{
	uint16_t i;
	// 解锁Flash
	FLASH_Unlock();
	// 清除标记位
	FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
	// 擦除当前页
	FLASH_ErasePage(addr);

	// 半字(2字节)循环写入
	for (i = 0; i < len; i += 2)
	{
		FLASH_ProgramHalfWord(addr + i, *(uint16_t *)(pbuf + i));
	}
	// 上锁Flash
	FLASH_Lock();
}

/* ------------------- bootloader菜单函数 ------------------- */

#define PACKET_SIZE 128U								// 真正固件数据长度
#define PACKET_PER_PAGE (stm32_page_size / PACKET_SIZE) // 一页16包

/* -----
收到 16 包 (16 × 128 = 2048 字节)，RAM 缓存刚好凑齐一页，执行一次 Flash 擦除 + 烧写，避免每收 128 字节就擦 Flash。
---- */
extern usart_data U0CB;

/* ================= CODEX ADD BEGIN ================= */

#define W25Q_FW_SLOT_SIZE 0x10000u
#define W25Q_FW_DATA_OFFSET 0x100u
#define W25Q_FW_MAGIC 0x57463235u

#define W25Q_FW_SLOT_ADDR(n) (w25q_fw_store_start + \
							  ((uint32_t)(n) - 1u) * W25Q_FW_SLOT_SIZE)

#define W25Q_FW_DATA_ADDR(n) (W25Q_FW_SLOT_ADDR(n) + W25Q_FW_DATA_OFFSET)

typedef __packed struct
{
	uint32_t magic;
	uint32_t length;
	uint32_t crc32;
	uint16_t slot;
	uint8_t version[16];
	uint8_t reserved[6];
} w25q_fw_header_t;

static uint8_t s_fw_page_buf[stm32_page_size];

static void fw_ring_pop(void)
{
	if (U0CB.URxDataOUT != U0CB.URxDataIN)
	{
		U0CB.URxDataOUT++;
		if (U0CB.URxDataOUT == U0CB.URxDataEND)
		{
			U0CB.URxDataOUT = &U0CB.URxDataPtr[0];
		}
	}
}

static uint32_t fw_crc32_update(uint32_t crc,
								const uint8_t *buf,
								uint32_t len)
{
	uint32_t i;
	uint32_t j;

	for (i = 0; i < len; i++)
	{
		crc ^= buf[i];

		for (j = 0; j < 8; j++)
		{
			if (crc & 1u)
			{
				crc = (crc >> 1) ^ 0xEDB88320u;
			}
			else
			{
				crc >>= 1;
			}
		}
	}

	return crc;
}

static int fw_select_slot(void)
{
	uint16_t datalen;
	uint8_t *data;
	uint8_t number;

	while (1)
	{
		if (U0CB.URxDataOUT == U0CB.URxDataIN)
		{
			continue;
		}

		datalen = U0CB.URxDataOUT->end - U0CB.URxDataOUT->start + 1;
		data = U0CB.URxDataOUT->start;
		number = get_only_number(data, datalen);

		fw_ring_pop();

		if (number >= 1u && number <= 9u)
		{
			return (int)number;
		}

		tell("编号错误，请输入 1~9");
	}
}

/* ================= CODEX ADD END ================= */

void menu1(void)
{
	u1_printf("擦除A区\r\n");
	STM32_EraseFlash(stm32_a_start_page, stm32_a_page_num);
	u1_printf("擦除成功\r\n");
	uasrt_command_suf();
}

void menu2(void)
{
	/* ------------------- 数据弹出移动 ------------------- */
	U0CB.URxDataOUT++;
	if (U0CB.URxDataOUT == U0CB.URxDataEND)
	{
		U0CB.URxDataOUT = &U0CB.URxDataPtr[0];
	}

	u1_printf("通过Xmodem协议，串口IAP下载A区程序，请使用bin格式文件\r\n");
	STM32_EraseFlash(stm32_a_start_page, stm32_a_page_num);

	UpDataA.XmodemNB = 0;
	memset(UpDataA.Updatabuff, 0, stm32_page_size);

	/* ------------------- 发送 C 准备，30秒内应答 ------------------- */
	uint32_t wait_start_tick = 0;
	while (1)
	{
		if (get_tick() - wait_start_tick >= 1000)
		{
			printf("C");
			wait_start_tick = get_tick();
		}

		if (U0CB.URxDataOUT != U0CB.URxDataIN)
			break;
	}

	if (get_tick() - wait_start_tick >= 30000)
	{
		u1_printf("\r\nXmodem等待上位机超时，返回菜单\r\n");
		uasrt_command_suf();
		return;
	}

	/* ------------------- MCU开始接受一包 包头序号检验+CRC16检验 ------------------- */
	while (1)
	{
		// 如果环形缓冲区为空，直接跳过本次循环，不读取旧数据包
		if (U0CB.URxDataOUT == U0CB.URxDataIN)
		{
			continue;
		}
		uint16_t datalen = U0CB.URxDataOUT->end - U0CB.URxDataOUT->start + 1;
		uint8_t *data = U0CB.URxDataOUT->start;
		// 收到133字节完整数据包 SOH
		if ((datalen == 133) && (data[0] == 0x01))
		{

			/* ------------------- 包序号校验代码 ------------------- */
			uint8_t pkt_num = data[1];
			uint8_t pkt_num_inv = data[2];
			if ((pkt_num + pkt_num_inv) != 0xFF)
			{
				u1_printf("\x15");
				goto next_packet;
			}

			/* ------------------- CRC16校验代码 ------------------- */
			uint16_t calc_crc, recv_crc;
			calc_crc = Xmodem_CRC16(&data[3], 128);
			recv_crc = (data[131] << 8) | data[132]; // 高字节在前！

			if (calc_crc == recv_crc) // CRC16检验成功进入
			{
				UpDataA.XmodemNB++;
				// 拷贝128字节到RAM缓存
				uint16_t offset = ((UpDataA.XmodemNB - 1) % PACKET_PER_PAGE) * PACKET_SIZE;
				memcpy(&UpDataA.Updatabuff[offset], &data[3], 128); // 拷贝作用函数

				// 攒满一页2048字节，烧入Flash
				if ((UpDataA.XmodemNB % PACKET_PER_PAGE) == 0)
				{
					uint32_t write_addr;
					write_addr = stm32_a_saddr + ((UpDataA.XmodemNB / PACKET_PER_PAGE) - 1) * stm32_page_size;
					stm32_write_flash(write_addr, UpDataA.Updatabuff, stm32_page_size);
				}

				u1_printf("\x06"); // ACK
			}
			else
			{
				u1_printf("\x15"); // NAK
			}
			goto next_packet; // 【无条件，必走】这一包无论好坏，处理完都丢掉，移动 OUT 指针，读取下一包。
		}

		/* ------------------- 一包结束代码 ------------------- */
		// 收到EOT结束信号 0x04
		if ((datalen == 1) && (data[0] == 0x04))
		{
			u1_printf("\x06");

			Delay(200);
			// 检查 SRAM 缓存剩余不足一页的数据
			uint16_t remainder_packet = UpDataA.XmodemNB % PACKET_PER_PAGE;
			if (remainder_packet != 0)
			{
				uint32_t write_addr;
				write_addr = stm32_a_saddr + (UpDataA.XmodemNB / PACKET_PER_PAGE) * stm32_page_size;
				stm32_write_flash(write_addr, UpDataA.Updatabuff, remainder_packet * 128);

				u1_printf("/* -------------------------------------- */\r\n");
				u1_printf("下载成功\r\n");
				u1_printf("/* -------------------------------------- */\r\n");
				uasrt_command_suf();
			}

			return;
		}

	next_packet:
		// menu2内部自己移动读出指针，弹出数据包
		U0CB.URxDataOUT++;
		if (U0CB.URxDataOUT == U0CB.URxDataEND)
		{
			U0CB.URxDataOUT = &U0CB.URxDataPtr[0];
		}
	}
}

void menu3(void)
{
	/* ------------------- 数据弹出移动 ------------------- */
	U0CB.URxDataOUT++;
	if (U0CB.URxDataOUT == U0CB.URxDataEND)
	{
		U0CB.URxDataOUT = &U0CB.URxDataPtr[0];
	}

	u1_printf("/* -------------------------------------- */\r\n");
	u1_printf("在30秒内设置版本号，超时返回主菜单\r\n");
	u1_printf("/* -------------------------------------- */\r\n");

	/* ------------------- 30秒内设置版本号 (VER-1.0.0-2023/02/20-12:00)------------------- */
	uint32_t wait_start_tick = get_tick();
	while (1)
	{
		if (get_tick() - wait_start_tick < 30000)
		{

			if (U0CB.URxDataOUT != U0CB.URxDataIN)
				break;
		}
		else if (get_tick() - wait_start_tick >= 30000)
		{
			tell("Xmodem等待上位机超时，返回菜单");
			uasrt_command_suf();
			return;
		}
	}

	/* ------------------- MCU开始接受 ------------------- */
	while (1)
	{
		// 如果环形缓冲区为空，直接跳过本次循环，不读取旧数据包
		if (U0CB.URxDataOUT == U0CB.URxDataIN)
		{
			continue;
		}

		uint16_t datalen = U0CB.URxDataOUT->end - U0CB.URxDataOUT->start + 1;
		uint8_t *data = U0CB.URxDataOUT->start;

		if (U0CB.URxDataOUT != U0CB.URxDataIN)
		{
			// tell("收到数据包长度 = %d\r\n",datalen);
			if (datalen == 27)
			{
				uint32_t temp[8];
				// 校验字符串格式 VER‑x.x.x‑x/x/x‑x:x
				if (sscanf((char *)data, "VER-%d.%d.%d-%d/%d/%d-%d:%d", &temp[0], &temp[1], &temp[2], &temp[3], &temp[4], &temp[5], &temp[6], &temp[7]) == 8)
				{
					W25Q64_ReadData(w25q_ota_info_addr, (uint8_t *)&ota_info, ota_info_typedef_size);
					// 先清零版本数组，保证末尾自带'\0'结束符
					memset(ota_info.version, 0, sizeof(ota_info.version));
					// 拷贝26字节版本字符串
					memcpy(ota_info.version, data, datalen);
					// 写之前需要擦除
					W25Q64_SectorErase(w25q_ota_info_addr);
					// 写入W25Q64保存
					W25Q64_PageProgram(w25q_ota_info_addr, (uint8_t *)&ota_info, ota_info_typedef_size);

					tell("当前版本:%s\r\n", ota_info.version);
					tell("版本号设置成功!\r\n");
					uasrt_command_suf();
					return;
				}
				else
				{
					tell("版本号格式错误,请重新输入");
					/* ------------------- 数据弹出移动 ------------------- */
					U0CB.URxDataOUT++;
					if (U0CB.URxDataOUT == U0CB.URxDataEND)
					{
						U0CB.URxDataOUT = &U0CB.URxDataPtr[0];
					}
					continue;
				}
			}
			else
			{
				tell("版本号长度错误，请重新输入");
				/* ------------------- 数据弹出移动 ------------------- */
				U0CB.URxDataOUT++;
				if (U0CB.URxDataOUT == U0CB.URxDataEND)
				{
					U0CB.URxDataOUT = &U0CB.URxDataPtr[0];
				}
				continue;
			}
		}
	}
}

void menu4(void)
{
	W25Q64_ReadData(w25q_ota_info_addr, (uint8_t *)&ota_info, ota_info_typedef_size);
	tell("查询当前版本:%s\r\n", ota_info.version);
	uasrt_command_suf();
}
void menu5(void)
{
	uint8_t fw_num = 0;
	/* ------------------- 数据弹出移动 ------------------- */
	U0CB.URxDataOUT++;
	if (U0CB.URxDataOUT == U0CB.URxDataEND)
	{
		U0CB.URxDataOUT = &U0CB.URxDataPtr[0];
	}
	tell("向外部flash下载程序，输入需要使用的块编号 （1 ~ 9 ）");
	/* ------------------- MCU开始接受 ------------------- */
	while (1)
	{
		// 如果环形缓冲区为空，直接跳过本次循环，不读取旧数据包
		if (U0CB.URxDataOUT == U0CB.URxDataIN)
		{
			continue;
		}
		// 环形缓冲区有数据，开始读取
		__disable_irq();
		uint16_t datalen = U0CB.URxDataOUT->end - U0CB.URxDataOUT->start + 1;
		uint8_t *data = U0CB.URxDataOUT->start;
		__enable_irq();

		tell("收到数据包长度 = %d\r\n", datalen);
		if (datalen == 2)
		{
			uint8_t i;
			fw_num = 0;
			for (i = 0; i < 2; i++)
			{
				if (data[i] >= '1' && data[i] <= '9')
				{
					fw_num = data[i] - '0';
					break;
				}
			}
			if (fw_num >= 1 && fw_num <= 9)
			{
				tell("输入程序块编号:%d\r\n", fw_num);
				/* ------------------- 数据弹出移动 ------------------- */
				U0CB.URxDataOUT++;
				if (U0CB.URxDataOUT == U0CB.URxDataEND)
				{
					U0CB.URxDataOUT = &U0CB.URxDataPtr[0];
				}
				break;
			}
			else
			{
				tell("输入编号格式错误，请重新输入");
				goto next_packet;
			}
		}
		else
		{
			tell("输入长度错误，请重新输入");
			goto next_packet;
		}
	next_packet:
		/* ------------------- 数据弹出移动 ------------------- */
		U0CB.URxDataOUT++;
		if (U0CB.URxDataOUT == U0CB.URxDataEND)
		{
			U0CB.URxDataOUT = &U0CB.URxDataPtr[0];
		}
		continue;
	}
	uint32_t w25q_write_base_addr = 0;
	/* ------------------- 根据编号选择W25Q起始地址 ------------------- */
	switch (fw_num)
	{
	case 1:
		w25q_write_base_addr = fw1_addr;
		break;
	case 2:
		w25q_write_base_addr = fw2_addr;
		break;
	case 3:
		w25q_write_base_addr = fw3_addr;
		break;
	case 4:
		w25q_write_base_addr = fw4_addr;
		break;
	case 5:
		w25q_write_base_addr = fw5_addr;
		break;
	case 6:
		w25q_write_base_addr = fw6_addr;
		break;
	case 7:
		w25q_write_base_addr = fw7_addr;
		break;
	case 8:
		w25q_write_base_addr = fw8_addr;
		break;
	case 9:
		w25q_write_base_addr = fw9_addr;
		break;
	default:
		uasrt_command_suf();
		return;
	}

	//===========新增：下载固件前擦除W25Q对应分区============
	u1_printf("正在擦除W25Q固件分区...\r\n");
	w25q_erase_64k_block(w25q_write_base_addr);
	W25Q64_WaitBusy();
	//=======================================================

	UpDataA.XmodemNB = 0; // Xmodem包计数器清零
	u1_printf("/* -------------------------------------- */\r\n");
	u1_printf("在30秒内等待数据包，超时返回主菜单\r\n");
	u1_printf("/* -------------------------------------- */\r\n");
	/* ------------------- 发送 C 准备，30秒内应答 ------------------- */
	uint32_t wait_start_tick = get_tick();
	uint32_t send_c_tick = get_tick();

	while (1)
	{
		if (get_tick() - send_c_tick >= 1000)
		{
			printf("C");
			send_c_tick = get_tick();
		}
		if (U0CB.URxDataOUT != U0CB.URxDataIN)
			break;
		// 30秒超时检测
		if (get_tick() - wait_start_tick >= 30000)
		{
			u1_printf("\r\nXmodem等待上位机超时，返回菜单\r\n");
			uasrt_command_suf();
			return;
		}
	}

	/* ------------------- MCU开始接受一包 包头序号检验+CRC16检验 ------------------- */
	while (1)
	{
		// 如果环形缓冲区为空，直接跳过本次循环，不读取旧数据包
		if (U0CB.URxDataOUT == U0CB.URxDataIN)
		{
			continue;
		}
		__disable_irq();
		uint16_t datalen = U0CB.URxDataOUT->end - U0CB.URxDataOUT->start + 1;
		uint8_t *data = U0CB.URxDataOUT->start;
		__enable_irq();

		// 收到133字节完整数据包 SOH
		if ((datalen == 133) && (data[0] == 0x01))
		{
			/* ------------------- 包序号校验代码 ------------------- */
			uint8_t pkt_num = data[1];
			uint8_t pkt_num_inv = data[2];
			uint8_t expect_pkt = UpDataA.XmodemNB + 1;

			if ((pkt_num + pkt_num_inv) != 0xFF || pkt_num != expect_pkt)
			{
				u1_printf("\x15");
				goto next_p;
			}
			/* ------------------- CRC16校验代码 ------------------- */
			uint16_t calc_crc, recv_crc;
			calc_crc = Xmodem_CRC16(&data[3], 128);
			recv_crc = (data[131] << 8) | data[132]; // 高字节在前！
			if (calc_crc == recv_crc)				 // CRC16检验成功进入
			{
				UpDataA.XmodemNB++;
				//=== 改动1：数据包直接写入W25Q，取消原先STM32‑Flash写入逻辑 ===
				uint32_t pkt_offset = (UpDataA.XmodemNB - 1) * PACKET_SIZE;
				W25Q64_PageProgram(w25q_write_base_addr + pkt_offset, &data[3], 128);
				W25Q64_WaitBusy();
				u1_printf("\x06"); // ACK
			}
			else
			{
				u1_printf("\x15"); // NAK
			}
			goto next_p; // 【无条件，必走】这一包无论好坏，处理完都丢掉，移动 OUT 指针，读取下一包。
		}
		/* ------------------- 一包结束代码 ------------------- */
		// 收到EOT结束信号 0x04
		if ((datalen == 1) && (data[0] == 0x04))
		{
			u1_printf("\x06");
			Delay(200);
			uint32_t fw_length = UpDataA.XmodemNB * PACKET_SIZE;
			uint8_t buf[sizeof(ota_info_typedef)];
			W25Q64_ReadData(w25q_ota_info_addr, buf, sizeof(ota_info_typedef));
			ota_info_typedef *temp_ota = (ota_info_typedef *)buf;
			temp_ota->fw_len[fw_num - 1] = fw_length;

			W25Q64_SectorErase(w25q_ota_info_addr);
			W25Q64_WaitBusy();
			W25Q64_PageProgram(w25q_ota_info_addr, buf, sizeof(ota_info_typedef));
			W25Q64_WaitBusy();
			u1_printf("/* -------------------------------------- */\r\n");
			u1_printf("固件下载写入W25Q成功！总包数:%d 固件长度:%lu\r\n", UpDataA.XmodemNB, fw_length);
			u1_printf("/* -------------------------------------- */\r\n");
			uasrt_command_suf();
			return;
		}
	next_p:
		// menu2内部自己移动读出指针，弹出数据包
		U0CB.URxDataOUT++;
		if (U0CB.URxDataOUT == U0CB.URxDataEND)
		{
			U0CB.URxDataOUT = &U0CB.URxDataPtr[0];
		}
	}
}
void menu6(void)
{
	
	
	uint8_t fw_num = 0;
	U0CB.URxDataOUT++;
	if (U0CB.URxDataOUT == U0CB.URxDataEND)
	{
		U0CB.URxDataOUT = &U0CB.URxDataPtr[0];
	}
	uint32_t wait_start_tick = get_tick();
	tell("选择固件搬运到A区运行，请输入编号 1~9");
	// 等待输入固件编号
	while (1)
	{
		if (get_tick() - wait_start_tick >= 30000)
		{
			u1_printf("\r\n等待输入超时\r\n");
			uasrt_command_suf();
			return;
		}
		if (U0CB.URxDataOUT != U0CB.URxDataIN)
		{
			uint16_t datalen = U0CB.URxDataOUT->end - U0CB.URxDataOUT->start + 1;
			uint8_t *data = U0CB.URxDataOUT->start;
			tell("收到数据包长度 = %d\r\n", datalen);
			if (datalen == 2)
			{
				uint8_t i;
				fw_num = 0;
				for (i = 0; i < 2; i++)
				{
					if (data[i] >= '1' && data[i] <= '9')
					{
						fw_num = data[i] - '0';
						break;
					}
				}
				if (fw_num >= 1 && fw_num <= 9)
				{
					tell("输入程序块编号:%d\r\n", fw_num);
					/* ------------------- 数据弹出移动 ------------------- */
					U0CB.URxDataOUT++;
					if (U0CB.URxDataOUT == U0CB.URxDataEND)
					{
						U0CB.URxDataOUT = &U0CB.URxDataPtr[0];
					}
					break;
				}
				else
				{
					tell("输入编号格式错误，请重新输入");
					/* ------------------- 数据弹出移动 ------------------- */
					U0CB.URxDataOUT++;
					if (U0CB.URxDataOUT == U0CB.URxDataEND)
					{
						U0CB.URxDataOUT = &U0CB.URxDataPtr[0];
					}
					continue;
				}
			}
			else
			{
				tell("输入长度错误，请重新输入");
				/* ------------------- 数据弹出移动 ------------------- */
				U0CB.URxDataOUT++;
				if (U0CB.URxDataOUT == U0CB.URxDataEND)
				{
					U0CB.URxDataOUT = &U0CB.URxDataPtr[0];
				}
				continue;
			}
		}
	}

	uint8_t buf[sizeof(ota_info_typedef)];
	W25Q64_ReadData(w25q_ota_info_addr, buf, sizeof(ota_info_typedef));
	ota_info_typedef *temp_ota = (ota_info_typedef *)buf;

	u1_printf("开始搬运固件%d到A区,固件长度:%lu\r\n", fw_num, temp_ota->fw_len[fw_num - 1]);
	uint8_t ret = copy_w25q_to_app(fw_num, temp_ota->fw_len[fw_num - 1]);
	if (ret != 0)
	{
		u1_printf("搬运失败！\r\n");
		uasrt_command_suf();
		return;
	}
	u1_printf("搬运完成，准备跳转APP\r\n");
	Delay(500);
	jump_to_app();
}

void menu7(void)
{
	u1_printf("2秒后重启\r\n");
	Delay(2000);
	NVIC_SystemReset();
}

void menu8(void)
{
	tell("跳转进入A区");
	jump_to_app();
}

void menu9(void)
{
	tell("远程接收消息开始，输入0结束");

	/* ------------------- 数据弹出移动 ------------------- */
	U0CB.URxDataOUT++;
	if (U0CB.URxDataOUT == U0CB.URxDataEND)
	{
		U0CB.URxDataOUT = &U0CB.URxDataPtr[0];
	}

	while (1)
	{
		if (mqtt_line_ready == 1)
		{
			mqtt_parse_sub_recv(mqtt_line_buf);
			mqtt_line_ready = 0;

			if (g_mqtt_msg_ready == 1)
			{
				g_mqtt_msg_ready = 0;
				tell("收到消息:%s", g_mqtt_sub_msg.payload);
			}
		}

		if (U0CB.URxDataOUT != U0CB.URxDataIN)
		{
			uint16_t datalen = U0CB.URxDataOUT->end - U0CB.URxDataOUT->start + 1;
			uint8_t *data = U0CB.URxDataOUT->start;

			uint8_t menu_val1 = get_only_number(data, datalen);

			switch (menu_val1)
			{
			case 0:
				tell("退出");
				uasrt_command_suf();
				return;

			default:
				tell("输入的指令无效，请重新输入");

				/* ------------------- 数据弹出移动 ------------------- */
				U0CB.URxDataOUT++;
				if (U0CB.URxDataOUT == U0CB.URxDataEND)
				{
					U0CB.URxDataOUT = &U0CB.URxDataPtr[0];
				}

				break;
			}
		}
	}
}

void menu_error(void)
{
	tell("输入的指令无效，请重新输入");
	uasrt_command_suf();
}

/* ------------------- bootloader菜单功能选择函数 ------------------- */
void bootloader_event(uint8_t *data, uint16_t datalen)
{
	if (datalen == 0)
	{
		return;
	}
	uint8_t menu_val = get_only_number(data, datalen);

	switch (menu_val)
	{
	case 1:
		menu1();
		break;
	case 2:
		menu2();
		break;
	case 3:
		menu3();
		break;
	case 4:
		menu4();
		break;
	case 5:
		menu5();
		break;
	case 6:
		menu6();
		break;
	case 7:
		menu7();
		break;
	case 8:
		menu8();
		break;
	case 9:
		menu9();
		break;
	default:
		// 没有数字 / 不是1‑7，进入menu8
		menu_error();
		break;
	}
}
