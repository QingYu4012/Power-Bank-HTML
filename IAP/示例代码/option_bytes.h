#ifndef     __OPTION_BYTES_H_
#define     __OPTION_BYTES_H_

#include "stdint.h"

/* 注意：
1.避免多次擦写Flash，用到该选项字节时，上电后必须读取选项字节数据一次，将Flash存储的选项字节copy到RAM中处理后续逻辑

int main(void)
{
	// 读取选项字节数据
	Option_Bytes_Read_Data();
	后续程序
	...
}

2.为了避免错误的操作选项字节，只能使用引出的接口控制，禁止将Option_Bytes变量extern出去
	获取选项字节：Option_Bytes_Get_...
	写入选项字节：Option_Bytes_Set_...

3.当退出BootLoader时，必须写入选项字节数据一次，将RAM存储的选项字节copy到Flash中

	Option_Bytes_Send_Data();

	Jump_To_APP();

*/

/* 烧录程序时是否写入应用层密钥，BootLoader工程默认不在镜像内放置选项字节 */
#ifndef OPTION_BYTE_APP_KEY_ENABLE
#define OPTION_BYTE_APP_KEY_ENABLE          (0)
#endif


/* Ymodem文件名最长保存64字节，避免选项字节超过一页Flash */
#define OPTION_BYTES_FILE_NAME_MAX_LEN      (64)

typedef enum{
	BOOT_STATE_READY = 0,
	BOOT_STATE_APP_ERROR,
	BOOT_STATE_APP_REQUEST_UPGRAD,
	BOOT_STATE_APP_UPDATA,
	BOOT_STATE_APP_UPDATA_READY,
	BOOT_STATE_JUMP_APP,
	BOOT_STATE_APP,
	BOOT_STATE_NUM,
}Boot_State_TypeDef;


typedef enum{
	OPTION_BYTES_PROTOCOL_NONE = 0,
	OPTION_BYTES_PROTOCOL_XMODEM_128,
	OPTION_BYTES_PROTOCOL_XMODEM_1K,
	OPTION_BYTES_PROTOCOL_YMODEM_128,
	OPTION_BYTES_PROTOCOL_YMODEM_1K,
}Option_Bytes_Protocol_TypeDef;

/* 读取选项字节数据 */
void Option_Bytes_Read_Data(void);

/* 写入选项字节数据 */
void Option_Bytes_Send_Data(void);

/* 对选项字节进行CRC32校验 */
uint32_t Option_Bytes_Data_CRC32_Calculate(void);

/* 获取选项字节数据指针 */
uint32_t *Option_Bytes_Get_Data_Point(void);

/* 获取选项字节长度 */
uint8_t Option_Bytes_Get_Size(void);

/* 获取选项字节 应用层密钥 */
uint32_t Option_Bytes_Get_APP_KEY(void);

/* 设置选项字节 应用层密钥 */
void Option_Bytes_Set_APP_KEY(uint32_t APP_Key);

/* 获取选项字节 引导状态 */
Boot_State_TypeDef Option_Bytes_Get_BOOT_State(void);

/* 设置选项字节 引导状态 */
void Option_Bytes_Set_BOOT_State(Boot_State_TypeDef BOOT_State);

/* 获取选项字节 应用层文件大小 */
uint32_t Option_Bytes_Get_APP_SIZE(void);

/* 设置选项字节 应用层文件大小 */
void Option_Bytes_Set_APP_SIZE(uint32_t APP_Size);

/* 获取选项字节 应用层文件CRC-32校验 */
uint32_t Option_Bytes_Get_APP_CRC32(void);

/* 设置选项字节 应用层文件CRC-32校验 */
void Option_Bytes_Set_APP_CRC32(uint32_t APP_CRC32);

/* 获取选项字节CRC-32校验 */
uint32_t Option_Bytes_Get_OB_CRC32(void);

/* 设置选项字节CRC-32校验 */
void Option_Bytes_Set_OB_CRC32(uint32_t OB_CRC32);


/* 获取选项字节 文件传输协议 */
Option_Bytes_Protocol_TypeDef Option_Bytes_Get_File_Protocol(void);

/* 获取选项字节 Ymodem原始文件大小 */
uint32_t Option_Bytes_Get_File_Size(void);

/* 获取选项字节 文件名长度 */
uint32_t Option_Bytes_Get_File_Name_Len(void);

/* 获取选项字节 文件名指针 */
uint8_t *Option_Bytes_Get_File_Name_Point(void);

/* 清除选项字节 文件信息 */
void Option_Bytes_Clear_File_Info(void);

/* 设置选项字节 文件信息 */
void Option_Bytes_Set_File_Info(Option_Bytes_Protocol_TypeDef Protocol, const uint8_t *File_Name, uint32_t File_Name_Len, uint32_t File_Size);

#endif
