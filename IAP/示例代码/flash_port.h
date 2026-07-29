#ifndef     __FLASH_PORT_H_
#define     __FLASH_PORT_H_

#include "system.h"

/* 解锁Flash写保护，底层HAL会按页再次解锁 */
void FLASH_UnlockAllPages(void);

/* 锁定Flash写保护，底层HAL会按页再次锁定 */
void FLASH_LockAllPages(void);

/* 按地址范围擦除Flash页，EndAddr为包含关系 */
uint8_t FLASH_ErasePages(uint32_t StartAddr, uint32_t EndAddr);

/* 按字节写入Flash并回读校验 */
uint8_t FLASH_WriteBytes(uint32_t Address, const uint8_t *Data, uint32_t Len);

/* 按32位字写入Flash并回读校验 */
uint8_t FLASH_WriteWords(uint32_t Address, const uint32_t *Data, uint32_t Len);

#endif
