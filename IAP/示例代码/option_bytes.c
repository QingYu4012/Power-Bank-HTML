#include "option_bytes.h"
#include "string.h"
#include "crc.h"
#include "iap.h"
#include "flash_port.h"

#if (OPTION_BYTE_APP_KEY_ENABLE == 1)
const uint32_t __APP_KEY__          __attribute__((at(APP_OPTION_BYTES_ADDR))) = APP_READY_KEY;
const uint32_t __BOOT_STATE__       __attribute__((at(APP_OPTION_BYTES_ADDR + 4))) = BOOT_STATE_READY;
const uint32_t __APP_SIZE__         __attribute__((at(APP_OPTION_BYTES_ADDR + 8))) = 0x00000000;
const uint32_t __APP_CRC32__        __attribute__((at(APP_OPTION_BYTES_ADDR + 12))) = 0xFFFFFFFF;
const uint32_t __OB_CRC32__         __attribute__((at(APP_OPTION_BYTES_ADDR + 16))) = 0x3B1BE76E;
const uint32_t __FILE_PROTOCOL__    __attribute__((at(APP_OPTION_BYTES_ADDR + 20))) = OPTION_BYTES_PROTOCOL_NONE;
const uint32_t __FILE_SIZE__        __attribute__((at(APP_OPTION_BYTES_ADDR + 24))) = 0x00000000;
const uint32_t __FILE_NAME_LEN__    __attribute__((at(APP_OPTION_BYTES_ADDR + 28))) = 0x00000000;
const uint32_t __FILE_NAME__[OPTION_BYTES_FILE_NAME_MAX_LEN / 4] __attribute__((at(APP_OPTION_BYTES_ADDR + 32))) = {0};
/* APP可用空间由当前固件分区配置生成 */
const uint32_t __APP_CAPACITY__     __attribute__((at(APP_OPTION_BYTES_ADDR + 96))) = IAP_APP_MAX_SIZE;
#endif

typedef struct {
	union
	{
		struct {
			uint32_t APP_Key;           /* 应用层密钥 */
			uint32_t BOOT_State;        /* 引导程序状态 */
			uint32_t APP_Size;          /* 应用层文件大小 */
			uint32_t APP_CRC32;         /* 应用层文件CRC-32校验 */
			uint32_t OB_CRC32;          /* 选项字节CRC-32校验 */
			uint32_t File_Protocol;     /* 文件传输协议 */
			uint32_t File_Size;         /* Ymodem原始文件大小 */
			uint32_t File_Name_Len;     /* 文件名长度 */
			uint8_t File_Name[OPTION_BYTES_FILE_NAME_MAX_LEN]; /* 文件名 */
			/* 当前固件APP可用存放空间 */
			uint32_t APP_Capacity;
		};
		uint8_t Byte[36 + OPTION_BYTES_FILE_NAME_MAX_LEN];
		uint32_t Data[9 + (OPTION_BYTES_FILE_NAME_MAX_LEN / 4)];
	};
}Option_Bytes_TypeDef;

volatile Option_Bytes_TypeDef Option_Bytes = {0};

/* 读取选项字节数据 */
void Option_Bytes_Read_Data(void)
{
	memcpy((uint8_t *)Option_Bytes.Data, (uint8_t *)APP_OPTION_BYTES_ADDR, sizeof(Option_Bytes_TypeDef));
	/* APP容量始终使用当前固件分区配置，兼容旧版Flash中没有该字段的选项字节 */
	Option_Bytes.APP_Capacity = IAP_APP_MAX_SIZE;
}

/* 写入选项字节数据 */
void Option_Bytes_Send_Data(void)
{
	FLASH_UnlockAllPages();
	FLASH_ErasePages(APP_OPTION_BYTES_ADDR, APP_OPTION_BYTES_ADDR + FLASH_PAGE_SIZE - 1);
	FLASH_WriteWords(APP_OPTION_BYTES_ADDR, (uint32_t *)Option_Bytes.Data, sizeof(Option_Bytes_TypeDef) / 4);
	FLASH_LockAllPages();
}

/* 对选项字节进行CRC32校验 */
uint32_t Option_Bytes_Data_CRC32_Calculate(void)
{
	uint8_t *Data = (uint8_t *)Option_Bytes.Byte;
	uint32_t CRC_Result = Data_CRC32_Calculate(0, Data, 16);
	/* OB_CRC32自身不参与校验，APP容量为编译期只读配置，保持旧96字节结构的CRC兼容 */
	CRC_Result = Data_CRC32_Calculate(CRC_Result, &Data[20], 12 + OPTION_BYTES_FILE_NAME_MAX_LEN);
	return CRC_Result;
}

/* 获取选项字节数据指针 */
uint32_t *Option_Bytes_Get_Data_Point(void)
{
	return (uint32_t *)Option_Bytes.Data;
}

/* 获取选项字节长度 */
uint8_t Option_Bytes_Get_Size(void)
{
	return sizeof(Option_Bytes_TypeDef);
}

/* 获取当前固件APP可用存放空间 */
uint32_t Option_Bytes_Get_APP_Capacity(void)
{
	return Option_Bytes.APP_Capacity;
}

/* 获取选项字节 应用层密钥 */
uint32_t Option_Bytes_Get_APP_KEY(void)
{
	return Option_Bytes.APP_Key;
}

/* 设置选项字节 应用层密钥 */
void Option_Bytes_Set_APP_KEY(uint32_t APP_Key)
{
	Option_Bytes.APP_Key = APP_Key;
	/* 更新APP选项字节数据后都需要更新选项字节的校验 */
	Option_Bytes_Set_OB_CRC32(Option_Bytes_Data_CRC32_Calculate());
}

/* 获取选项字节 引导状态 */
Boot_State_TypeDef Option_Bytes_Get_BOOT_State(void)
{
	return (Boot_State_TypeDef)Option_Bytes.BOOT_State;
}

/* 设置选项字节 引导状态 */
void Option_Bytes_Set_BOOT_State(Boot_State_TypeDef BOOT_State)
{
	Option_Bytes.BOOT_State = BOOT_State;
	/* 更新APP选项字节数据后都需要更新选项字节的校验 */
	Option_Bytes_Set_OB_CRC32(Option_Bytes_Data_CRC32_Calculate());
}

/* 获取选项字节 应用层文件大小 */
uint32_t Option_Bytes_Get_APP_SIZE(void)
{
	return Option_Bytes.APP_Size;
}

/* 设置选项字节 应用层文件大小 */
void Option_Bytes_Set_APP_SIZE(uint32_t APP_Size)
{
	Option_Bytes.APP_Size = APP_Size;
	/* 更新APP选项字节数据后都需要更新选项字节的校验 */
	Option_Bytes_Set_OB_CRC32(Option_Bytes_Data_CRC32_Calculate());
}

/* 获取选项字节 应用层文件CRC-32校验 */
uint32_t Option_Bytes_Get_APP_CRC32(void)
{
	return  Option_Bytes.APP_CRC32;
}

/* 设置选项字节 应用层文件CRC-32校验 */
void Option_Bytes_Set_APP_CRC32(uint32_t APP_CRC32)
{
	Option_Bytes.APP_CRC32 = APP_CRC32;
	/* 更新APP选项字节数据后都需要更新选项字节的校验 */
	Option_Bytes_Set_OB_CRC32(Option_Bytes_Data_CRC32_Calculate());
}

/* 获取选项字节CRC-32校验 */
uint32_t Option_Bytes_Get_OB_CRC32(void)
{
	return Option_Bytes.OB_CRC32;
}

/* 设置选项字节CRC-32校验 */
void Option_Bytes_Set_OB_CRC32(uint32_t OB_CRC32)
{
	Option_Bytes.OB_CRC32 = OB_CRC32;
}

/* 获取选项字节 文件传输协议 */
Option_Bytes_Protocol_TypeDef Option_Bytes_Get_File_Protocol(void)
{
	return (Option_Bytes_Protocol_TypeDef)Option_Bytes.File_Protocol;
}

/* 获取选项字节 Ymodem原始文件大小 */
uint32_t Option_Bytes_Get_File_Size(void)
{
	return Option_Bytes.File_Size;
}

/* 获取选项字节 文件名长度 */
uint32_t Option_Bytes_Get_File_Name_Len(void)
{
	return Option_Bytes.File_Name_Len;
}

/* 获取选项字节 文件名指针 */
uint8_t *Option_Bytes_Get_File_Name_Point(void)
{
	return (uint8_t *)Option_Bytes.File_Name;
}

/* 清除选项字节 文件信息 */
void Option_Bytes_Clear_File_Info(void)
{
	Option_Bytes_Set_File_Info(OPTION_BYTES_PROTOCOL_NONE, 0, 0, 0);
}

/* 设置选项字节 文件信息 */
void Option_Bytes_Set_File_Info(Option_Bytes_Protocol_TypeDef Protocol, const uint8_t *File_Name, uint32_t File_Name_Len, uint32_t File_Size)
{
	uint32_t Len = File_Name_Len;
	if (Len > OPTION_BYTES_FILE_NAME_MAX_LEN)
		Len = OPTION_BYTES_FILE_NAME_MAX_LEN;

	Option_Bytes.File_Protocol = Protocol;
	Option_Bytes.File_Size = File_Size;
	Option_Bytes.File_Name_Len = Len;
	memset((uint8_t *)Option_Bytes.File_Name, 0, sizeof(Option_Bytes.File_Name));
	if (File_Name != 0 && Len != 0)
		memcpy((uint8_t *)Option_Bytes.File_Name, File_Name, Len);
	/* 文件信息写入后同步更新选项字节CRC，确保APP校验和信息区校验一致 */
	Option_Bytes_Set_OB_CRC32(Option_Bytes_Data_CRC32_Calculate());
}
