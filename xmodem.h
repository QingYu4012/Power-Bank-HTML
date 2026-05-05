#ifndef		__XMODEM_H_
#define		__XMODEM_H_

#include "system.h"
#include "iap.h"

#define XMODEM_CRC_ENABLE						(1)
#define XMODEM_REQUEST_TRY_TIME					(500)//请求数据尝试时间，单位：mS
#define XMODEM_RUNNING_TIMEOVER					(5000)//请求数据尝试时间，单位：mS


#define Xmodem_Send(__VAL__)					UART_SendData_8bit(CW_UART2, __VAL__)


#define Xmodem_Flash_Init()						IAP_Flash_Init()
#define Xmodem_Flash_Proc(__VAL1__, __VAL2__, __VAL3__)				IAP_Flash_Proc(__VAL1__, __VAL2__, __VAL3__)
#define Xmodem_Flash_DeInit()					IAP_Flash_DeInit()

#define XMODEM_DATA_SIZE						(3 + 128 + 1 + XMODEM_CRC_ENABLE)//128
#define XMODEM_1K_DATA_SIZE						(3 + 1024 + 1 + XMODEM_CRC_ENABLE)//1024

typedef enum{
	XMODEM_STATE_READ_IDLE = 0,
	XMODEM_STATE_READ_RUN,
	XMODEM_STATE_READ_READY,
	XMODEM_STATE_READ_TRY,
	XMODEM_STATE_READ_ERROR,
	XMODEM_STATE_READ_END,
	XMODEM_STATE_NUM,
}Xmodem_State_TypeDef;

typedef struct{
	Xmodem_State_TypeDef State;
	
	uint8_t Header;
	uint8_t Xmodem_Mode;
	uint8_t Packet_Number;
	uint8_t Last_Number;
	uint8_t Verify_Mode;
	uint16_t Verify;
	uint8_t Error_Count;
}Xmodem_TypeDef;

/* Xmodem 复位 */
void Xmodem_Reset(void);

/* Xmodem 读取数据 */
void Xmodem_Read(uint8_t *Data, uint16_t Len);

/* Xmodem 程序处理 */
void Xmodem_Proc(void);


#endif
