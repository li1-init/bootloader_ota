#include "stm32f10x.h"                  // Device header
#include "flash.h"    

/**
  * @brief  擦除Flash多个页面
  * @param  start: 起始偏移地址(字节偏移，从0开始)  例：0x8008000偏移就是32768
  * @param  num:   需要擦除的页数  1页 = 2048字节
  */
void STM32_EraseFlash(uint32_t start, uint16_t num)
{
	uint16_t i;
	FLASH_Unlock();					//解锁内部Flash，允许擦写
	
	for(i = 0; i < num; i++)
	{
        // 基地址0x08000000 + 偏移 + 每页2048*i
		FLASH_ErasePage( (0x08000000 + start) + (2048 * i) );
	}
	
	FLASH_Lock();					//写完上锁，禁止改写
}

/**
  * @brief  以32位(4字节)为单位写入内部Flash
  * @param  saddr:   要写入的绝对地址，例如 0x08008000
  * @param  wdata:   数据源指针，32位数组
  * @param  wnum:    需要写入的字节总数量！！
  */
void STM32_WriteFlash(uint32_t saddr, uint32_t *wdata, uint32_t wnum)
{
	FLASH_Unlock();					//解锁Flash
	
	while(wnum > 0)
	{
		FLASH_ProgramWord(saddr, *wdata);	//写入1个32位数据(4字节)
		wnum -= 4;					        //剩余字节数?4
		saddr += 4;					        //地址向后移动4字节
		wdata++;					        //数据源指针向后移动
	}
	
	FLASH_Lock();					//上锁
}


