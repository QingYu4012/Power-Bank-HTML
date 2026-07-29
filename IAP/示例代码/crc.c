#include "crc.h"

#define CRC8_POLYNOMIAL_CCITT      (0x07)
#define CRC16_POLYNOMIAL_CCITT     (0x1021)
#define CRC32_POLYNOMIAL           (0xEDB88320UL)

/* CRC8-CCITT逐位计算，校验范围由上层传入 */
uint8_t Data_CRC8_Verify(uint8_t Init_Value, const uint8_t *Data, uint32_t Len)
{
	uint8_t CRC = Init_Value;
	uint32_t i = 0;
	uint8_t j = 0;

	if (Data == 0)
		return CRC;

	for (i = 0; i < Len; i++)
	{
		CRC ^= Data[i];
		for (j = 0; j < 8; j++)
		{
			if (CRC & 0x80)
				CRC = (CRC << 1) ^ CRC8_POLYNOMIAL_CCITT;
			else
				CRC <<= 1;
		}
	}
	return CRC;
}

/* Xmodem CRC16逐位计算，多包传输固定使用0作为初值 */
uint16_t Data_CRC16_Verify(uint16_t Init_Value, const uint8_t *Data, uint32_t Len)
{
	uint16_t CRC = Init_Value;
	uint32_t i = 0;
	uint8_t j = 0;

	if (Data == 0)
		return CRC;

	for (i = 0; i < Len; i++)
	{
		CRC ^= (uint16_t)Data[i] << 8;
		for (j = 0; j < 8; j++)
		{
			if (CRC & 0x8000)
				CRC = (CRC << 1) ^ CRC16_POLYNOMIAL_CCITT;
			else
				CRC <<= 1;
		}
	}
	return CRC;
}

/* 标准CRC32计算，Init_Value传入上一段返回值即可连续计算 */
uint32_t Data_CRC32_Calculate(uint32_t Init_Value, const uint8_t *Data, uint32_t Len)
{
	uint32_t CRC = Init_Value ^ 0xFFFFFFFFUL;
	uint32_t i = 0;
	uint8_t j = 0;

	if (Data == 0)
		return Init_Value;

	for (i = 0; i < Len; i++)
	{
		CRC ^= Data[i];
		for (j = 0; j < 8; j++)
		{
			if (CRC & 0x00000001UL)
				CRC = (CRC >> 1) ^ CRC32_POLYNOMIAL;
			else
				CRC >>= 1;
		}
	}
	return CRC ^ 0xFFFFFFFFUL;
}
