#ifndef     __XYMODEM_H_
#define     __XYMODEM_H_

#include "system.h"
#include "iap.h"

#define XYMODEM_CRC_ENABLE                       (1)
#define XYMODEM_REQUEST_TRY_TIME                 (500)       /* 请求数据尝试时间，单位：mS */
#define XYMODEM_RUNNING_TIMEOVER                 (10000)     /* 接收过程超时时间，单位：mS */
#define XYMODEM_MAX_ERROR_COUNT                  (10)        /* 连续错误最大重试次数 */

#define XYmodem_Flash_Init()                     IAP_Flash_Init()
#define XYmodem_Flash_Proc(__VAL1__, __VAL2__, __VAL3__, __VAL4__)     IAP_Flash_Proc(__VAL1__, __VAL2__, __VAL3__, __VAL4__)
#define XYmodem_Flash_DeInit()                   IAP_Flash_DeInit()

#define XYMODEM_DATA_128_LEN                     (128)
#define XYMODEM_DATA_1K_LEN                      (1024)
#define XYMODEM_DATA_SIZE                        (3 + XYMODEM_DATA_128_LEN + 2)
#define XYMODEM_1K_DATA_SIZE                     (3 + XYMODEM_DATA_1K_LEN + 2)

typedef enum{
	XYMODEM_STATE_READ_IDLE = 0,
	XYMODEM_STATE_READ_RUN,
	XYMODEM_STATE_READ_READY,
	XYMODEM_STATE_READ_TRY,
	XYMODEM_STATE_READ_ERROR,
	XYMODEM_STATE_READ_END,
	XYMODEM_STATE_NUM,
}XYmodem_State_TypeDef;

typedef enum{
	XYMODEM_PROTOCOL_XMODEM = 0,
	XYMODEM_PROTOCOL_YMODEM,
}XYmodem_Protocol_TypeDef;

typedef struct{
	XYmodem_State_TypeDef State;
	XYmodem_Protocol_TypeDef Protocol;

	uint8_t Header;
	uint8_t XYmodem_Mode;
	uint8_t Packet_Number;
	uint8_t Last_Number;
	uint8_t Verify_Mode;
	uint16_t Verify;
	uint8_t Error_Count;
	uint32_t Block_Count;
	uint32_t File_Size;
	uint32_t File_Offset;
}XYmodem_TypeDef;

/* XYmodem复位 */
void XYmodem_Reset(void);

/* XYmodem读取数据 */
void XYmodem_Read(uint8_t *Data, uint16_t Len);

/* XYmodem程序处理 */
void XYmodem_Proc(void);

#endif
