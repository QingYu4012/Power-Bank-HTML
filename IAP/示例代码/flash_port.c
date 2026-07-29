#include "flash_port.h"

/* 底层HAL在擦写时会自动处理页保护，这里保留接口兼容原有代码 */
void FLASH_UnlockAllPages(void)
{
	(void)0;
}

/* 底层HAL在擦写后会自动恢复页保护，这里保留接口兼容原有代码 */
void FLASH_LockAllPages(void)
{
	(void)0;
}

/* 检查Flash地址范围，防止擦写越过芯片末尾 */
static uint8_t FLASH_Port_Check_Range(uint32_t Address, uint32_t Len)
{
	if (Len == 0)
		return 0;
	if (Address >= FLASH_SIZE_64K)
		return 0;
	if (Len > (FLASH_SIZE_64K - Address))
		return 0;
	return 1;
}

/* 按页擦除Flash，起止地址会自动对齐到512字节页 */
uint8_t FLASH_ErasePages(uint32_t StartAddr, uint32_t EndAddr)
{
	FLASH_EraseInitTypeDef Erase_Init = {0};
	uint32_t Page_Error = 0;
	uint32_t Start_Page = 0;
	uint32_t End_Page = 0;

	if (StartAddr > EndAddr)
		return 0x01;
	if (StartAddr >= FLASH_SIZE_64K || EndAddr >= FLASH_SIZE_64K)
		return 0x01;

	Start_Page = StartAddr & ~(FLASH_PAGE_SIZE - 1);
	End_Page = EndAddr & ~(FLASH_PAGE_SIZE - 1);

	Erase_Init.TypeErase = FLASH_TYPEERASE_PAGES;
	Erase_Init.PageAddress = Start_Page;
	Erase_Init.NbPages = ((End_Page - Start_Page) / FLASH_PAGE_SIZE) + 1;

	return (HAL_FLASH_Erase(&Erase_Init, &Page_Error) == HAL_OK) ? 0 : 0x01;
}

/* 按字节写入Flash并逐字节回读，避免静默写入失败 */
uint8_t FLASH_WriteBytes(uint32_t Address, const uint8_t *Data, uint32_t Len)
{
	uint32_t i = 0;

	if (Data == 0 || FLASH_Port_Check_Range(Address, Len) == 0)
		return 0x01;

	for (i = 0; i < Len; i++)
	{
		if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, Address + i, Data[i]) != HAL_OK)
			return 0x01;
		if (*(__IO uint8_t *)(Address + i) != Data[i])
			return 0x01;
	}
	return 0;
}

/* 按32位字写入Flash并逐字回读，供选项字节保存使用 */
uint8_t FLASH_WriteWords(uint32_t Address, const uint32_t *Data, uint32_t Len)
{
	uint32_t i = 0;

	if (Data == 0 || FLASH_Port_Check_Range(Address, Len * 4) == 0)
		return 0x01;
	if ((Address & 0x03) != 0)
		return 0x01;

	for (i = 0; i < Len; i++)
	{
		if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, Address + (i * 4), Data[i]) != HAL_OK)
			return 0x01;
		if (*(__IO uint32_t *)(Address + (i * 4)) != Data[i])
			return 0x01;
	}
	return 0;
}
