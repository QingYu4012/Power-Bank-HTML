#ifndef     __CRC_H_
#define     __CRC_H_

#include "stdint.h"

/* CRC8-CCITT校验，初值由调用者传入 */
uint8_t Data_CRC8_Verify(uint8_t Init_Value, const uint8_t *Data, uint32_t Len);

/* Xmodem CRC16校验，初值由调用者传入 */
uint16_t Data_CRC16_Verify(uint16_t Init_Value, const uint8_t *Data, uint32_t Len);

/* 标准CRC32校验，支持按上一段返回值继续累加 */
uint32_t Data_CRC32_Calculate(uint32_t Init_Value, const uint8_t *Data, uint32_t Len);

#endif
