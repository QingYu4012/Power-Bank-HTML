#ifndef     __IAP_CMD_H_
#define     __IAP_CMD_H_

#include "system.h"

#define IAP_CMD_DATA_BUFF_SIZE          (32)

typedef enum{
	DATA_PACK_CMD_GET_ACK = 0,
	DATA_PACK_CMD_GET_OPTION_BYTES,
	DATA_PACK_CMD_SET_BOOT_STATE,
	DATA_PACK_CMD_NUM,
}Data_Pack_CMD_TypeDef;

/* IAP CMD复位 */
void IAP_CMD_Rrset(void);

/* IAP CMD接收数据 */
void IAP_CMD_Receive_Data_Pack(uint8_t *Data, uint16_t Len);

#endif
