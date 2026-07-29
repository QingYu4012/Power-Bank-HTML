#include "xymodem.h"
#include "crc.h"
#include "uart.h"

/* XYmodem */
/*************************************************************************
---------------------------------------------------------------------------
|     Byte1     |    Byte2    |     Byte3      |Byte4~Byte131|  Byte132   |
|-------------------------------------------------------------------------|
|Start Of Header|Packet Number|~(Packet Number)| Packet Data |  Check Sum |
---------------------------------------------------------------------------
**************************************************************************/

/* 1K - XYmodem */
/*************************************************************************
---------------------------------------------------------------------------
|     Byte1     |    Byte2    |     Byte3      |Byte4~Byte1027|  Byte1028 |
|-------------------------------------------------------------------------|
|Start Of Header|Packet Number|~(Packet Number)| Packet Data |  Check Sum |
---------------------------------------------------------------------------
**************************************************************************/

#define XYMODEM_CMD_SOH                         (0x01)      /* 128字节数据传输 */
#define XYMODEM_CMD_STX                         (0x02)      /* 1024字节数据传输 */
#define XYMODEM_CMD_EOT                         (0x04)      /* 结束 */

#define XYMODEM_CMD_ACK                         (0x06)      /* 应答 */
#define XYMODEM_CMD_NAK                         (0x15)      /* 重传 */
#define XYMODEM_CMD_NAK_CRC                     ('C')       /* 以CRC16方式请求数据 */
#define XYMODEM_CMD_CAN                         (0x18)      /* 无条件结束 */

#define XYMODEM_PACKET_HEADER_SIZE              (3)
#define XYMODEM_PACKET_SUM_SIZE                 (1)
#define XYMODEM_PACKET_CRC_SIZE                 (2)

XYmodem_TypeDef XYmodem = {XYMODEM_STATE_READ_IDLE};
static uint8_t XYmodem_RX_Buff[XYMODEM_1K_DATA_SIZE] = {0};
static uint16_t XYmodem_RX_Count = 0;
static uint16_t XYmodem_RX_Expect_Len = 0;

/* XYmodem发送单字节控制码 */
static void XYmodem_Send(uint8_t Data)
{
	Uart_Transmit((char *)&Data, 1);
}

/* XYmodem接收缓存复位 */
static void XYmodem_RX_Reset(void)
{
	XYmodem_RX_Count = 0;
	XYmodem_RX_Expect_Len = 0;
}

/* XYmodem复位 */
void XYmodem_Reset(void)
{
	XYmodem_TypeDef XYmodem_NULL = {XYMODEM_STATE_READ_IDLE};
	XYmodem = XYmodem_NULL;
	XYmodem_RX_Reset();
}

/* 累加和校验 */
static uint8_t Data_Sum_Verify(uint8_t *Data, uint16_t Len)
{
	uint16_t i = 0;
	uint8_t Result = 0;

	for (i = 0; i < Len; i++)
		Result += Data[i];
	return Result;
}

/* 获取XYmodem数据区长度 */
static uint16_t XYmodem_Get_Data_Len(uint8_t Header)
{
	return (Header == XYMODEM_CMD_SOH) ? XYMODEM_DATA_128_LEN : XYMODEM_DATA_1K_LEN;
}

/* 判断当前包使用CRC16还是累加和校验 */
static uint8_t XYmodem_Get_Verify_Mode(uint8_t Header, uint16_t Len)
{
	uint16_t Data_Len = XYmodem_Get_Data_Len(Header);
	if (Len == (XYMODEM_PACKET_HEADER_SIZE + Data_Len + XYMODEM_PACKET_CRC_SIZE))
		return 1;
	return 0;
}

/* 判断包长度是否匹配SOH/STX和校验模式 */
static uint8_t XYmodem_Check_Packet_Len(uint8_t Header, uint16_t Len)
{
	uint16_t Data_Len = XYmodem_Get_Data_Len(Header);
	if (Len == (XYMODEM_PACKET_HEADER_SIZE + Data_Len + XYMODEM_PACKET_CRC_SIZE))
		return 1;
	if (Len == (XYMODEM_PACKET_HEADER_SIZE + Data_Len + XYMODEM_PACKET_SUM_SIZE))
		return 1;
	return 0;
}

/* IAP升级固定使用CRC16包长度，避免C/NAK交替时把CRC包误按累加和包提前处理 */
static uint16_t XYmodem_Get_Expect_Packet_Len(uint8_t Header)
{
	uint16_t Data_Len = XYmodem_Get_Data_Len(Header);
#if (XYMODEM_CRC_ENABLE == 1)
	return XYMODEM_PACKET_HEADER_SIZE + Data_Len + XYMODEM_PACKET_CRC_SIZE;
#else
	return XYMODEM_PACKET_HEADER_SIZE + Data_Len + XYMODEM_PACKET_SUM_SIZE;
#endif
}

/* 校验XYmodem数据包 */
static uint8_t XYmodem_Verify_Packet(uint8_t *Data, uint16_t Len)
{
	uint16_t Data_Len = XYmodem_Get_Data_Len(Data[0]);
	uint16_t Index = XYMODEM_PACKET_HEADER_SIZE + Data_Len;
	uint16_t Verify_Num = 0;

	XYmodem.Verify_Mode = XYmodem_Get_Verify_Mode(Data[0], Len);
	if (XYmodem.Verify_Mode)
	{
		Verify_Num = ((uint16_t)Data[Index] << 8) | Data[Index + 1];
		XYmodem.Verify = Data_CRC16_Verify(0, &Data[3], Data_Len);
	}
	else
	{
		Verify_Num = Data[Index];
		XYmodem.Verify = Data_Sum_Verify(&Data[3], Data_Len);
	}
	return (Verify_Num == XYmodem.Verify);
}

/* 根据首包头和传输协议换算选项字节中的协议类型 */
static Option_Bytes_Protocol_TypeDef XYmodem_Get_Option_Protocol(uint8_t Header, XYmodem_Protocol_TypeDef Protocol)
{
	if (Protocol == XYMODEM_PROTOCOL_YMODEM)
		return (Header == XYMODEM_CMD_SOH) ? OPTION_BYTES_PROTOCOL_YMODEM_128 : OPTION_BYTES_PROTOCOL_YMODEM_1K;
	return (Header == XYMODEM_CMD_SOH) ? OPTION_BYTES_PROTOCOL_XMODEM_128 : OPTION_BYTES_PROTOCOL_XMODEM_1K;
}

/* 解析Ymodem首包中的文件名和文件大小 */
static uint8_t Ymodem_Parse_File_Info(uint8_t *Data, uint16_t Data_Len, uint32_t *File_Size, uint32_t *File_Name_Len)
{
	uint16_t i = 0;
	uint32_t Size = 0;
	uint8_t Has_Size = 0;

	if (Data == 0 || File_Size == 0 || File_Name_Len == 0 || Data[0] == 0)
		return 0;

	while (i < Data_Len && Data[i] != 0)
		i++;
	if (i == 0 || ++i >= Data_Len)
		return 0;
	*File_Name_Len = i - 1;

	while (i < Data_Len && Data[i] == ' ')
		i++;
	while (i < Data_Len && Data[i] >= '0' && Data[i] <= '9')
	{
		Has_Size = 1;
		Size = (Size * 10) + (Data[i] - '0');
		i++;
	}
	if (Has_Size == 0 || Size == 0)
		return 0;
	*File_Size = Size;
	return 1;
}
/* 处理Ymodem文件信息包 */
static void Ymodem_Header_Proc(uint8_t *Data, uint16_t Len)
{
	uint16_t Data_Len = XYmodem_Get_Data_Len(Data[0]);
	uint32_t File_Size = 0;
	uint32_t File_Name_Len = 0;

	if (XYmodem_Verify_Packet(Data, Len) == 0)
	{
		XYmodem.State = XYMODEM_STATE_READ_TRY;
		return;
	}

	/* Ymodem空文件名包表示传输彻底结束，收到该包后再完成APP校验和选项字节写入 */
	if (Data[3] == 0)
	{
		XYmodem_Send(XYMODEM_CMD_ACK);
		if (XYmodem.Protocol == XYMODEM_PROTOCOL_YMODEM)
			XYmodem_Flash_DeInit();
		XYmodem_Reset();
		return;
	}

	if (Ymodem_Parse_File_Info(&Data[3], Data_Len, &File_Size, &File_Name_Len) == 0 || File_Size > IAP_APP_MAX_SIZE)
	{
		XYmodem.State = XYMODEM_STATE_READ_ERROR;
		return;
	}

	if (XYmodem_Flash_Init() != 0)
	{
		XYmodem.State = XYMODEM_STATE_READ_ERROR;
		return;
	}

	XYmodem.Protocol = XYMODEM_PROTOCOL_YMODEM;
	Option_Bytes_Set_File_Info(XYmodem_Get_Option_Protocol(Data[0], XYMODEM_PROTOCOL_YMODEM), &Data[3], File_Name_Len, File_Size);
	XYmodem.File_Size = File_Size;
	XYmodem.File_Offset = 0;
	XYmodem.Block_Count = 0;
	XYmodem.Last_Number = 0;
	XYmodem_Send(XYMODEM_CMD_ACK);
	XYmodem_Send(XYMODEM_CMD_NAK_CRC);
	XYmodem.State = XYMODEM_STATE_READ_RUN;
}

/* 处理XYmodem/Ymodem数据包 */
static void XYmodem_Data_Packet_Proc(uint8_t *Data, uint16_t Len)
{
	uint16_t Data_Len = XYmodem_Get_Data_Len(Data[0]);
	uint16_t Write_Len = Data_Len;
	uint8_t Expected_Number = XYmodem.Last_Number + 1;

	if (XYmodem.State == XYMODEM_STATE_READ_IDLE && Data[1] == 0x01)
	{
		XYmodem.Protocol = XYMODEM_PROTOCOL_XMODEM;
		XYmodem.Block_Count = 0;
		XYmodem.File_Size = 0;
		XYmodem.File_Offset = 0;
		XYmodem.Last_Number = 0;
		XYmodem.State = (XYmodem_Flash_Init() == 0) ? XYMODEM_STATE_READ_RUN : XYMODEM_STATE_READ_ERROR;
		if (XYmodem.State == XYMODEM_STATE_READ_RUN)
			Option_Bytes_Set_File_Info(XYmodem_Get_Option_Protocol(Data[0], XYMODEM_PROTOCOL_XMODEM), 0, 0, 0);
	}

	if (XYmodem.State != XYMODEM_STATE_READ_RUN)
		return;

	/* 重复包只ACK不重复写Flash，避免主机重传导致CRC和文件大小被累加两次 */
	if (Data[1] == XYmodem.Last_Number && XYmodem.Block_Count != 0)
	{
		/* 重复包也按有效通信处理，刷新接收超时计时 */
		XYmodem.State = XYMODEM_STATE_READ_READY;
		return;
	}

	if (Data[1] != Expected_Number || Data[1] != (uint8_t)(~Data[2]))
	{
		XYmodem.State = XYMODEM_STATE_READ_TRY;
		return;
	}

	if (XYmodem_Verify_Packet(Data, Len) == 0)
	{
		XYmodem.State = XYMODEM_STATE_READ_TRY;
		return;
	}

	if (XYmodem.Protocol == XYMODEM_PROTOCOL_YMODEM)
	{
		if (XYmodem.File_Offset >= XYmodem.File_Size)
			Write_Len = 0;
		else if ((XYmodem.File_Offset + Write_Len) > XYmodem.File_Size)
			Write_Len = XYmodem.File_Size - XYmodem.File_Offset;
	}

	if (Write_Len != 0 && XYmodem_Flash_Proc(XYmodem.Block_Count + 1, Data[0], &Data[3], Write_Len) != 0)
	{
		XYmodem.State = XYMODEM_STATE_READ_ERROR;
		return;
	}

	XYmodem.Error_Count = 0;
	XYmodem.Last_Number = Data[1];
	XYmodem.Block_Count++;
	XYmodem.File_Offset += Write_Len;
	/* 每收到一个有效包后切换一次状态，确保XYmodem_Proc刷新运行超时计时 */
	XYmodem.State = XYMODEM_STATE_READ_READY;
}

/* 处理已经拼好的完整XYmodem/Ymodem数据包 */
static void XYmodem_Full_Packet_Proc(uint8_t *Data, uint16_t Len)
{
	if ((Data[0] != XYMODEM_CMD_SOH && Data[0] != XYMODEM_CMD_STX) || XYmodem_Check_Packet_Len(Data[0], Len) == 0)
	{
		if (XYmodem.State == XYMODEM_STATE_READ_RUN)
			XYmodem.State = XYMODEM_STATE_READ_TRY;
		return;
	}

	XYmodem.Header = Data[0];
	XYmodem.Packet_Number = Data[1];
	XYmodem.XYmodem_Mode = Data[0];

	/* 只有空闲或Ymodem结束阶段才把0号包当Ymodem文件头，XYmodem序号回绕到0时仍是数据包 */
	if (Data[1] == 0 && Data[2] == 0xFF && (XYmodem.State == XYMODEM_STATE_READ_IDLE || XYmodem.State == XYMODEM_STATE_READ_END))
		Ymodem_Header_Proc(Data, Len);
	else
		XYmodem_Data_Packet_Proc(Data, Len);
}

/* XYmodem读取数据 */
void XYmodem_Read(uint8_t *Data, uint16_t Len)
{
	uint16_t i = 0;
	uint8_t Byte = 0;

	if (Len == 0 || Data == 0)
		return ;

	for (i = 0; i < Len; i++)
	{
		Byte = Data[i];

		if (XYmodem_RX_Count == 0)
		{
			if (Byte == XYMODEM_CMD_CAN)
			{
				XYmodem_Reset();
				continue;
			}

			if (Byte == XYMODEM_CMD_EOT && XYmodem.State == XYMODEM_STATE_READ_RUN)
			{
				XYmodem_Send(XYMODEM_CMD_ACK);
				if (XYmodem.Protocol == XYMODEM_PROTOCOL_YMODEM)
				{
					/* Ymodem标准流程还需要接收结束空0包，不能在EOT后立即切换BOOT状态 */
					XYmodem_Send(XYMODEM_CMD_NAK_CRC);
					XYmodem.State = XYMODEM_STATE_READ_END;
				}
				else
				{
					XYmodem_Flash_DeInit();
					XYmodem_Reset();
				}
				continue;
			}

			if (Byte != XYMODEM_CMD_SOH && Byte != XYMODEM_CMD_STX)
			{
				if (XYmodem.State == XYMODEM_STATE_READ_RUN)
					XYmodem.State = XYMODEM_STATE_READ_TRY;
				continue;
			}

			/* WebSerial/USB转串口可能把1029字节拆成多段，这里按流式方式重新拼成完整包 */
			XYmodem_RX_Expect_Len = XYmodem_Get_Expect_Packet_Len(Byte);
		}

		if (XYmodem_RX_Count >= sizeof(XYmodem_RX_Buff))
		{
			XYmodem_RX_Reset();
			XYmodem.State = XYMODEM_STATE_READ_TRY;
			continue;
		}

		XYmodem_RX_Buff[XYmodem_RX_Count++] = Byte;
		if (XYmodem_RX_Expect_Len != 0 && XYmodem_RX_Count >= XYmodem_RX_Expect_Len)
		{
			XYmodem_Full_Packet_Proc(XYmodem_RX_Buff, XYmodem_RX_Count);
			XYmodem_RX_Reset();
		}
	}
}

/* XYmodem程序处理 */
void XYmodem_Proc(void)
{
	static uint16_t TimeOver = 0;
	static uint16_t Last_State = 0;

	if (Last_State != XYmodem.State)
	{
		TimeOver = 0;
		Last_State = XYmodem.State;
	}
	switch(XYmodem.State)
	{
		case XYMODEM_STATE_READ_IDLE:
		{
			if (System_Check_Time(&TimeOver, XYMODEM_REQUEST_TRY_TIME))
			{
#if (XYMODEM_CRC_ENABLE == 1)
				/* IAP上位机固定使用CRC16，空闲请求固定发送C，避免主机和BootLoader校验模式分歧 */
				XYmodem.Verify_Mode = 1;
				XYmodem_Send(XYMODEM_CMD_NAK_CRC);
#else
				XYmodem.Verify_Mode = 0;
				XYmodem_Send(XYMODEM_CMD_NAK);
#endif
			}
			break;
		}
		case XYMODEM_STATE_READ_RUN:
		case XYMODEM_STATE_READ_END:
			if (System_Check_Time(&TimeOver, XYMODEM_RUNNING_TIMEOVER))
			{
				/* 运行中超时优先请求主机重发，避免半包残留后直接CAN取消升级 */
				XYmodem_RX_Reset();
				XYmodem.State = XYMODEM_STATE_READ_TRY;
			}
			break;
		case XYMODEM_STATE_READ_READY:
		{
			XYmodem_Send(XYMODEM_CMD_ACK);
			XYmodem.State = XYMODEM_STATE_READ_RUN;
			break;
		}
		case XYMODEM_STATE_READ_TRY:
		{
			XYmodem_RX_Reset();
			if (++XYmodem.Error_Count >= XYMODEM_MAX_ERROR_COUNT)
			{
				XYmodem.Error_Count = 0;
				TimeOver = 0;
				XYmodem.State = XYMODEM_STATE_READ_ERROR;
			}
			else
			{
				XYmodem_Send(XYMODEM_CMD_NAK);
				XYmodem.State = XYMODEM_STATE_READ_RUN;
			}
			break;
		}
		default:
		case XYMODEM_STATE_NUM:
		case XYMODEM_STATE_READ_ERROR:
			if (System_Check_Time(&TimeOver, XYMODEM_REQUEST_TRY_TIME))
			{
				XYmodem_Send(XYMODEM_CMD_CAN);
				if (++XYmodem.Error_Count >= XYMODEM_MAX_ERROR_COUNT)
				{
					TimeOver = 0;
					XYmodem_Reset();
				}
			}
			break;
	}
}
