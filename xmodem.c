#include "xmodem.h"
#include "crc.h"
/* Xmodem */
/*************************************************************************
---------------------------------------------------------------------------
|     Byte1     |    Byte2    |     Byte3      |Byte4~Byte131|  Byte132   |
|-------------------------------------------------------------------------|
|Start Of Header|Packet Number|~(Packet Number)| Packet Data |  Check Sum |
---------------------------------------------------------------------------
**************************************************************************/

/* 1K - Xmodem */
/*************************************************************************
---------------------------------------------------------------------------
|     Byte1     |    Byte2    |     Byte3      |Byte4~Byte1027|  Byte1028 |
|-------------------------------------------------------------------------|
|Start Of Header|Packet Number|~(Packet Number)| Packet Data |  Check Sum |
---------------------------------------------------------------------------
**************************************************************************/


#define XMODEM_CMD_SOH							(0x01)//128字节数据传输
#define XMODEM_CMD_STX 							(0x02)//1024字节数据传输
#define XMODEM_CMD_EOT							(0x04)//结束

#define XMODEM_CMD_ACK							(0x06)//应答
#define XMODEM_CMD_NAK							(0x15)//重传
#define XMODEM_CMD_NAK_CRC						('C')//重传,以CRC16校验方式发送数据时以'C'来请求
#define XMODEM_CMD_CAN							(0x18)//无条件结束

Xmodem_TypeDef Xmodem = {XMODEM_STATE_READ_IDLE};

/* Xmodem 复位 */
void Xmodem_Reset(void)
{
	Xmodem_TypeDef Xmodem_NULL = {XMODEM_STATE_READ_IDLE};
	Xmodem = Xmodem_NULL;
}

uint8_t Data_Sum_Verify(uint8_t *Data, uint16_t Len)
{
	uint16_t i = 0;
	uint8_t Result = 0;
	
	for (i = 0; i < Len; i++)
		Result += Data[i];
	return Result;
}

/* Xmodem 读取数据 */
void Xmodem_Read(uint8_t *Data, uint16_t Len)
{
	if (Len == 0 || Data == NULL)
		return ;
	if (Xmodem.State == XMODEM_STATE_READ_IDLE || Xmodem.State == XMODEM_STATE_READ_RUN)
	{
		Xmodem.Header = Data[0];
		if (Xmodem.Header == XMODEM_CMD_SOH || Xmodem.Header == XMODEM_CMD_STX)
		{
			if ((Xmodem.Header == XMODEM_CMD_SOH && Len == XMODEM_DATA_SIZE) || (Xmodem.Header == XMODEM_CMD_STX && Len == XMODEM_1K_DATA_SIZE))
			{
				Xmodem.Packet_Number = Data[1];
				Xmodem.Xmodem_Mode = Xmodem.Header;
				
				/* 判断是否是数据包开启 */
				if (Xmodem.Packet_Number == 0x01)
				{
					Xmodem.Last_Number = 0;
					Xmodem.State = (Xmodem_Flash_Init() == 0) ? XMODEM_STATE_READ_RUN : XMODEM_STATE_READ_ERROR;
				}
				/* 判断数据包顺序 */
				if (Xmodem.Packet_Number == (uint8_t)(~Data[2]) && Xmodem.Packet_Number > Xmodem.Last_Number)
				{
					/* 校验 */
					uint16_t Verify_Num = 0;
					if (Xmodem.Verify_Mode)
					{
						uint16_t Index = 3 + ((Xmodem.Xmodem_Mode == XMODEM_CMD_SOH) ? 128 : 1024);
						Verify_Num = (Data[Index] << 8) | Data[Index + 1];
						/* CRC16校验 */
						Xmodem.Verify = Data_CRC16_Verify(0, &Data[3], (Xmodem.Xmodem_Mode == XMODEM_CMD_SOH) ? 128 : 1024);
					}
					else
					{
						uint16_t Index = 3 + ((Xmodem.Xmodem_Mode == XMODEM_CMD_SOH) ? 128 : 1024);
						Verify_Num = Data[Index];
						/* 和校验 */
						Xmodem.Verify = Data_Sum_Verify(&Data[3], (Xmodem.Xmodem_Mode == XMODEM_CMD_SOH) ? 128 : 1024);
					}
					
					/* 校验通过 */
					if (Verify_Num == Xmodem.Verify)
					{
						/* 清除错误计数 */
						Xmodem.Error_Count = 0;
						/* 存储数据 */
						Xmodem.State = (Xmodem_Flash_Proc(Xmodem.Packet_Number, Xmodem.Xmodem_Mode, &Data[3]) == 0) ? XMODEM_STATE_READ_READY : XMODEM_STATE_READ_ERROR;
					}
					else Xmodem.State = XMODEM_STATE_READ_TRY;
				}
				else Xmodem.State = XMODEM_STATE_READ_TRY;
			}
			else if (Xmodem.Last_Number != 0)
				Xmodem.State = XMODEM_STATE_READ_TRY;	
		}			
		/* 传输结束 */
		else if (Xmodem.State == XMODEM_STATE_READ_RUN && Data[0] == XMODEM_CMD_EOT)
		{
			Xmodem_Reset();
			Xmodem_Flash_DeInit();
		}
	}
	else Xmodem_Reset();
}

/* Xmodem 程序处理 */
void Xmodem_Proc(void)
{
	static uint16_t TimeOver = 0;
	static uint16_t Last_State = 0;
	
	if (Last_State != Xmodem.State)
	{
		TimeOver = 0;
		Last_State = Xmodem.State;
	}
	switch(Xmodem.State)
	{
		case XMODEM_STATE_READ_IDLE:
		{
			if (System_Check_Time(&TimeOver, XMODEM_REQUEST_TRY_TIME))
			{
				Xmodem.Verify_Mode = !Xmodem.Verify_Mode;
				Xmodem_Send(Xmodem.Verify_Mode ? XMODEM_CMD_NAK_CRC : XMODEM_CMD_NAK);
			}
			break;
		}
		case XMODEM_STATE_READ_RUN:
			if (System_Check_Time(&TimeOver, XMODEM_RUNNING_TIMEOVER))
				Xmodem.State = XMODEM_STATE_READ_ERROR;
			break;
		case XMODEM_STATE_READ_READY:
		{
			Xmodem_Send(XMODEM_CMD_ACK);
			Xmodem.State = XMODEM_STATE_READ_RUN;
			break;
		}
		case XMODEM_STATE_READ_TRY:
		{
			if (++Xmodem.Error_Count >= 4)
			{
				Xmodem.Error_Count = 0;
				TimeOver = 0;
				Xmodem.State = XMODEM_STATE_READ_ERROR;
			}
			else
			{
				Xmodem_Send(XMODEM_CMD_NAK);
				Xmodem.State = XMODEM_STATE_READ_RUN;
			}
			break;
		}
		default:
		case XMODEM_STATE_NUM:
		case XMODEM_STATE_READ_ERROR:
			if (System_Check_Time(&TimeOver, XMODEM_REQUEST_TRY_TIME))
			{
				Xmodem_Send(XMODEM_CMD_CAN);
				if (++Xmodem.Error_Count >= 4)
				{
					TimeOver = 0;
					Xmodem_Reset();
				}
			}
			break;
	}
}
