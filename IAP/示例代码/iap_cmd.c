#include "iap_cmd.h"
#include "crc.h"
#include "xymodem.h"
#include "iap.h"
#include "uart.h"

/*
命令格式：
Header0 | Header1 |  CMD  |  Len  |  Data  |  CRC8
0xAA    0x55        1byte   2byte    LEN      1byte
*/

typedef enum{
	IAP_CMD_STATE_READ_HEADER = 0,
	IAP_CMD_STATE_READ_CMD,
	IAP_CMD_STATE_READ_LEN,
	IAP_CMD_STATE_READ_DATA,
	IAP_CMD_STATE_READ_CRC8,
}IAP_CMD_State_TypeDef;

typedef struct{
	uint8_t Verify;
	uint16_t Count;
	IAP_CMD_State_TypeDef State;

	uint16_t Header;
	uint8_t CMD;
	uint16_t Len;
	uint8_t Data_Buff[IAP_CMD_DATA_BUFF_SIZE];
	uint8_t CRC8;
}IAP_CMD_TypeDef;

IAP_CMD_TypeDef IAP_CMD = {0};

static void DATA_PACK_CMD_Get_ACK_Proc(uint16_t Len, uint8_t *Data_Buff);                              /* 获取响应 */
static void DATA_PACK_CMD_Get_Option_Bytes_Proc(uint16_t Len, uint8_t *Data_Buff);                     /* 获取选项字节 */
static void DATA_PACK_CMD_Set_Boot_State(uint16_t Len, uint8_t *Data_Buff);                            /* 设置BOOT状态 */

typedef void CMD_CallBack_Proc(uint16_t Len, uint8_t *Data_Buff);
CMD_CallBack_Proc *CMD_CallBack[DATA_PACK_CMD_NUM] = {
	DATA_PACK_CMD_Get_ACK_Proc,
	DATA_PACK_CMD_Get_Option_Bytes_Proc,
	DATA_PACK_CMD_Set_Boot_State,
};

/* IAP CMD复位 */
void IAP_CMD_Rrset(void)
{
	memset((uint8_t *)&IAP_CMD, 0, sizeof(IAP_CMD_TypeDef));
}

/* IAP CMD发送数据 */
static void IAP_CMD_Send_Data(uint8_t *Data, uint16_t Len)
{
	Uart_Transmit((char *)Data, Len);
}

/* IAP CMD数据处理 */
void IAP_CMD_Data_Pack_Proc(uint8_t Data)
{
	switch (IAP_CMD.State)
	{
		case IAP_CMD_STATE_READ_HEADER:
		{
			if (IAP_CMD.Count == 0 && Data == 0xAA)
			{
				IAP_CMD.Header = 0xAA;
				IAP_CMD.Count = 1;
			}
			else if (IAP_CMD.Count == 1 && Data == 0x55)
			{
				IAP_CMD.Header = 0xAA55;
				IAP_CMD.Count = 0;
				IAP_CMD.State = IAP_CMD_STATE_READ_CMD;
			}
			else
			{
				IAP_CMD.Count = (Data == 0xAA) ? 1 : 0;
				IAP_CMD.Header = (Data == 0xAA) ? 0xAA : 0;
			}
			break;
		}
		case IAP_CMD_STATE_READ_CMD:
		{
			if (Data < DATA_PACK_CMD_NUM)
			{
				IAP_CMD.CMD = Data;
				IAP_CMD.Len = 0;
				IAP_CMD.State = IAP_CMD_STATE_READ_LEN;
				IAP_CMD.Verify = Data_CRC8_Verify(0xFF, &Data, 1);
			}
			else IAP_CMD_Rrset();
			break;
		}
		case IAP_CMD_STATE_READ_LEN:
		{
			if (IAP_CMD.Count == 0)
			{
				IAP_CMD.Len = Data;
				IAP_CMD.Count = 1;
				IAP_CMD.Verify = Data_CRC8_Verify(IAP_CMD.Verify, &Data, 1);
			}
			else if (IAP_CMD.Count == 1)
			{
				IAP_CMD.Len |= (uint16_t)Data << 8;
				if (IAP_CMD.Len > IAP_CMD_DATA_BUFF_SIZE)
				{
					IAP_CMD_Rrset();
				}
				else
				{
					IAP_CMD.Count = 0;
					IAP_CMD.State = (IAP_CMD.Len == 0) ? IAP_CMD_STATE_READ_CRC8 : IAP_CMD_STATE_READ_DATA;
					IAP_CMD.Verify = Data_CRC8_Verify(IAP_CMD.Verify, &Data, 1);
				}
			}
			break;
		}
		case IAP_CMD_STATE_READ_DATA:
		{
			IAP_CMD.Data_Buff[IAP_CMD.Count] = Data;
			IAP_CMD.Verify = Data_CRC8_Verify(IAP_CMD.Verify, &Data, 1);
			if (++IAP_CMD.Count >= IAP_CMD.Len)
				IAP_CMD.State = IAP_CMD_STATE_READ_CRC8;
			break;
		}
		case IAP_CMD_STATE_READ_CRC8:
		{
			if (Data == IAP_CMD.Verify)
				CMD_CallBack[IAP_CMD.CMD](IAP_CMD.Len, IAP_CMD.Data_Buff);
			IAP_CMD_Rrset();
			break;
		}
		default:
		{
			IAP_CMD_Rrset();
			break;
		}
	}
}

/* IAP CMD接收数据 */
void IAP_CMD_Receive_Data_Pack(uint8_t *Data, uint16_t Len)
{
	uint32_t i = 0;
	/* 单个命令数据包Data最大长度为32字节，总长度为6 + 32 */
	if (Data != 0 && Len >= 6 && Len <= (6 + IAP_CMD_DATA_BUFF_SIZE))
	{
		IAP_CMD_Rrset();
		for (i = 0; i < Len; i++)
			IAP_CMD_Data_Pack_Proc(Data[i]);
	}

#if defined(TD66S_T_BOOTLOADER) || defined(CW32L910_BOOTLOADER)
	if (Option_Bytes_Get_BOOT_State() == BOOT_STATE_APP_UPDATA)
		XYmodem_Read(Data, Len);
#endif
}

/* ******************************************** 数据包处理程序 ********************************************* */
static void DATA_PACK_CMD_Get_ACK_Proc(uint16_t Len, uint8_t *Data_Buff)                              /* 获取响应 */
{
	uint8_t Buff[] = {0xAA, 0x55, 0x00, 0x01, 0x00,  0x01, 0x2F};
	(void)Len;
	(void)Data_Buff;
	IAP_CMD_Send_Data(Buff, sizeof(Buff));
}

static void DATA_PACK_CMD_Get_Option_Bytes_Proc(uint16_t Len, uint8_t *Data_Buff)                     /* 获取选项字节 */
{
	uint8_t Buff[5 + 128 + 1] = {0xAA, 0x55, 0x01, 0x00, 0x00};
	uint8_t Option_Bytes_Size = Option_Bytes_Get_Size();
	uint32_t *Option_Bytes_Data = Option_Bytes_Get_Data_Point();
	(void)Len;
	(void)Data_Buff;

	Buff[3] = Option_Bytes_Size & 0xFF;
	Buff[4] = Option_Bytes_Size >> 8;
	memcpy(&Buff[5], (uint8_t *)Option_Bytes_Data, Option_Bytes_Size);
	Buff[5 + Option_Bytes_Size] = Data_CRC8_Verify(0xFF, &Buff[2], Option_Bytes_Size + 3);
	IAP_CMD_Send_Data(Buff, Option_Bytes_Size + 6);
}

static void DATA_PACK_CMD_Set_Boot_State(uint16_t Len, uint8_t *Data_Buff)                            /* 设置BOOT状态 */
{
	uint8_t Buff[] = {0xAA, 0x55, 0x02, 0x01, 0x00,  0x00, 0x00};
	Boot_State_TypeDef Boot_State = Option_Bytes_Get_BOOT_State();

#if defined(TD66S_T_BOOTLOADER) || defined(CW32L910_BOOTLOADER)
	/* BootLoader状态下只允许进入升级或跳转APP */
	if (Len == 1 && (Data_Buff[0] == BOOT_STATE_APP_UPDATA || Data_Buff[0] == BOOT_STATE_JUMP_APP))
	{
		Buff[5] = 1;
		Option_Bytes_Set_BOOT_State((Boot_State_TypeDef)Data_Buff[0]);
		if (Data_Buff[0] == BOOT_STATE_APP_UPDATA)
		{
			/* 上位机主动切入升级窗口时，复位XYmodem状态机后再等待首包 */
			XYmodem_Reset();
		}
		Option_Bytes_Send_Data();
	}
#endif

#if defined(TD66S_T_APP) || defined(CW32L910_APP)
	/* APP状态下只允许请求进入升级 */
	if (Len == 1 && Boot_State == BOOT_STATE_APP && Data_Buff[0] == BOOT_STATE_APP_REQUEST_UPGRAD)
	{
		Buff[5] = 1;
		System.Jump_BootLoader = 1;
	}
#else
	(void)Boot_State;
#endif

	Buff[6] = Data_CRC8_Verify(0xFF, &Buff[2], 4);
	IAP_CMD_Send_Data(Buff, sizeof(Buff));
}
