#ifndef     __IAP_H_
#define     __IAP_H_

#include "system.h"
#include "option_bytes.h"
#include "uart.h"

#define IAP_DEBUG_ENABLE                    (1)

#if (IAP_DEBUG_ENABLE == 1)
#define IAP_DEBUG(__VAL__, ...)             Uart_Printf(__VAL__, ##__VA_ARGS__)
#else
#define IAP_DEBUG(__VAL__, ...)             ((void)0)
#endif

#define APP_READY_KEY                       (0xAA55BBDDUL)
#define APP_START_ADDR                      (0x00006000UL)
#define APP_OPTION_BYTES_ADDR               (0x0000FE00UL)
#define APP_FLASH_END_ADDR                  (APP_OPTION_BYTES_ADDR)
#define IAP_APP_MAX_SIZE                    (APP_FLASH_END_ADDR - APP_START_ADDR)
#define IAP_SRAM_START_ADDR                 (0x20000000UL)
#define IAP_SRAM_END_ADDR                   (0x20002000UL)

/* 初始化IAP程序 */
void IAP_Init(void);

/* IAP处理程序 */
void IAP_Proc(void);

/* 烧录前准备 */
uint8_t IAP_Flash_Init(void);

/* IAP程序编程 */
uint8_t IAP_Flash_Proc(uint32_t Packet_Number, uint8_t XYmodem_Mode, uint8_t *Data, uint16_t Len);

/* 烧录完成 */
void IAP_Flash_DeInit(void);

/* 烧录完成跳转到APP */
void IAP_Jump_To_APP(void);

#endif
