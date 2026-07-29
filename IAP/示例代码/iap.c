#include "iap.h"
#include "crc.h"
#include "xymodem.h"
#include "flash_port.h"

#if defined(TD66S_T_BOOTLOADER) || defined(CW32L910_BOOTLOADER)      /* IAP_BOOTLOADER_CODE_BEGIN */
volatile uint8_t APP_UPDATA_READY = 0;
static uint32_t IAP_Write_Offset = 0;

/* 校验APP栈顶和复位向量，支持JLink直接烧录APP后的最小跳转判断 */
static uint8_t IAP_APP_Vector_Is_Valid(void)
{
    uint32_t Stack_Address = *(__IO uint32_t *)APP_START_ADDR;
    uint32_t Reset_Address = *(__IO uint32_t *)(APP_START_ADDR + 4);
    uint32_t Reset_Address_Mask = Reset_Address & 0xFFFFFFFEUL;

    if ((Stack_Address < IAP_SRAM_START_ADDR) || (Stack_Address > IAP_SRAM_END_ADDR) || ((Stack_Address & 0x03) != 0))
        return 0;
    if ((Reset_Address & 0x01) == 0)
        return 0;
    if (Reset_Address_Mask < APP_START_ADDR || Reset_Address_Mask >= APP_FLASH_END_ADDR)
        return 0;
    return 1;
}

/* 校验APP栈顶、复位向量、文件大小和CRC32是否合法 */
static uint8_t IAP_APP_Is_Valid(uint8_t Check_Key)
{
    uint32_t APP_Size = Option_Bytes_Get_APP_SIZE();
    uint32_t APP_CRC32 = 0;

    if (IAP_APP_Vector_Is_Valid() == 0)
        return 0;
    if (Check_Key && Option_Bytes_Get_APP_KEY() != APP_READY_KEY)
        return 0;
    if (APP_Size < 8 || APP_Size > IAP_APP_MAX_SIZE)
        return 0;

    APP_CRC32 = Data_CRC32_Calculate(0, (uint8_t *)APP_START_ADDR, APP_Size);
    if (APP_CRC32 != Option_Bytes_Get_APP_CRC32())
        return 0;
    return 1;
}

#endif
/* 初始化IAP程序 */
void IAP_Init(void)
{
    Option_Bytes_Read_Data();
    IAP_DEBUG("Get Option Bytes\r\n");
    IAP_DEBUG("APP_KEY \t: %#x\r\n", Option_Bytes_Get_APP_KEY());
    IAP_DEBUG("BOOT_State\t: %#x\r\n", Option_Bytes_Get_BOOT_State());
    IAP_DEBUG("APP_SIZE\t: %#x\r\n", Option_Bytes_Get_APP_SIZE());
    IAP_DEBUG("APP_CRC32\t: %#x\r\n", Option_Bytes_Get_APP_CRC32());
    IAP_DEBUG("OB_CRC32\t: %#x\r\n", Option_Bytes_Get_OB_CRC32());
    IAP_DEBUG("FILE_PROTOCOL\t: %#x\r\n", Option_Bytes_Get_File_Protocol());
    IAP_DEBUG("FILE_SIZE\t: %#x\r\n", Option_Bytes_Get_File_Size());

#if defined(TD66S_T_APP) || defined(CW32L910_APP)
    if (Option_Bytes_Get_BOOT_State() != BOOT_STATE_APP)
    {
        Option_Bytes_Set_BOOT_State(BOOT_STATE_APP);
        Option_Bytes_Send_Data();
    }
#endif
}

#if defined(TD66S_T_APP) || defined(CW32L910_APP)
/* IAP处理程序 */
void IAP_Proc(void)
{
    static uint16_t TimeOver = 0;

    if (System.Jump_BootLoader == 1)
    {
        if (System_Check_Time(&TimeOver, 50))
        {
            /* APP只请求进入BootLoader，APP合法性仍由BootLoader负责判断 */
            Option_Bytes_Set_BOOT_State(BOOT_STATE_APP_REQUEST_UPGRAD);
            Option_Bytes_Send_Data();
            NVIC_SystemReset();
        }
    }
    else TimeOver = 0;
}

/* 烧录前准备 */
uint8_t IAP_Flash_Init(void)
{
    return 0;
}

/* IAP程序编程 */
uint8_t IAP_Flash_Proc(uint32_t Packet_Number, uint8_t XYmodem_Mode, uint8_t *Data, uint16_t Len)
{
    (void)Packet_Number;
    (void)XYmodem_Mode;
    (void)Data;
    (void)Len;
    return 0;
}

/* 烧录完成 */
void IAP_Flash_DeInit(void)
{

}

#endif

#if defined(TD66S_T_BOOTLOADER) || defined(CW32L910_BOOTLOADER)
/* IAP处理程序 */
void IAP_Proc(void)
{
    static uint16_t TimeOver = 0;
    static Boot_State_TypeDef Last_State = BOOT_STATE_READY;

    if (Last_State != Option_Bytes_Get_BOOT_State())
    {
        TimeOver = 0;
        Last_State = Option_Bytes_Get_BOOT_State();
    }

    switch (Option_Bytes_Get_BOOT_State())
    {
        /* 就绪 */
        case BOOT_STATE_APP:
        case BOOT_STATE_READY:
        {
            /* 判断选项字节和APP镜像是否正确 */
            uint32_t CRC_Result = Option_Bytes_Data_CRC32_Calculate();
            IAP_DEBUG("CRC Verify\t: %#x\r\n", CRC_Result);
            IAP_DEBUG("OB_CRC32\t: %#x\r\n", Option_Bytes_Get_OB_CRC32());
    IAP_DEBUG("FILE_PROTOCOL\t: %#x\r\n", Option_Bytes_Get_File_Protocol());
    IAP_DEBUG("FILE_SIZE\t: %#x\r\n", Option_Bytes_Get_File_Size());
            if (CRC_Result == Option_Bytes_Get_OB_CRC32() && IAP_APP_Is_Valid(1))
            {
                /* 校验通过允许跳转到APP */
                Option_Bytes_Set_BOOT_State(BOOT_STATE_JUMP_APP);
                IAP_DEBUG("BOOT_STATE_JUMP_APP\r\n");
            }
            else if (Option_Bytes_Get_APP_KEY() == APP_READY_KEY && IAP_APP_Vector_Is_Valid())
            {
                /* 兼容JLink直接烧录APP：必须带有APP_READY_KEY，避免IAP半包时按向量表误跳转 */
                Option_Bytes_Set_BOOT_State(BOOT_STATE_JUMP_APP);
                IAP_DEBUG("BOOT_STATE_JLINK_APP\r\n");
            }
            else
            {
                /* 校验不通过进入APP异常状态，避免跳转到损坏程序 */
                Option_Bytes_Set_APP_KEY(0x00000000);
                Option_Bytes_Set_BOOT_State(BOOT_STATE_APP_ERROR);
                Option_Bytes_Send_Data();
                IAP_DEBUG("BOOT_STATE_APP_ERROR\r\n");
            }
            break;
        }
        /* APP程序更新完成 */
        case BOOT_STATE_APP_UPDATA_READY:
        {
            APP_UPDATA_READY = 1;
            /* 等待上位机收完ACK后再跳转 */
            if (System_Check_Time(&TimeOver, 1000))
            {
                Option_Bytes_Set_BOOT_State(BOOT_STATE_READY);
                IAP_DEBUG("BOOT_STATE_READY\r\n");
            }
            break;
        }
        /* APP请求更新程序 */
        case BOOT_STATE_APP_REQUEST_UPGRAD:
        {
            /*
             * APP已明确请求升级，BootLoader上电后直接进入升级状态。
             * 避免上位机复位后再次设置状态的时间窗口过短，导致误跳回APP。
             */
            Option_Bytes_Set_BOOT_State(BOOT_STATE_APP_UPDATA);
            /* 进入升级窗口时复位XYmodem状态机，避免继承上一次异常传输状态 */
            XYmodem_Reset();
            Option_Bytes_Send_Data();
            IAP_DEBUG("BOOT_STATE_APP_UPDATA\r\n");
            break;
        }
        /* APP程序出现异常 */
        case BOOT_STATE_APP_ERROR:
        {
            /* APP异常后等待上位机连接，再自动进入升级状态 */
            if (System_Check_Time(&TimeOver, 10000))
            {
                Option_Bytes_Set_BOOT_State(BOOT_STATE_APP_UPDATA);
                /* APP异常后重新进入升级窗口，先复位XYmodem状态机 */
                XYmodem_Reset();
                Option_Bytes_Send_Data();
                IAP_DEBUG("BOOT_STATE_APP_UPDATA\r\n");
            }
            break;
        }
        /* APP程序更新 */
        case BOOT_STATE_APP_UPDATA:
        {
            APP_UPDATA_READY = 0;
            /* XYmodem/Ymodem程序处理 */
            XYmodem_Proc();
            break;
        }
        /* 跳转到APP程序 */
        case BOOT_STATE_JUMP_APP:
        {
            IAP_Jump_To_APP();
            /* 异常退出 */
            Option_Bytes_Set_BOOT_State(BOOT_STATE_APP_ERROR);
            Option_Bytes_Send_Data();
            IAP_DEBUG("BOOT_STATE_APP_ERROR\r\n");
            break;
        }
        default:
        {
            if (Option_Bytes_Get_APP_KEY() == APP_READY_KEY && IAP_APP_Vector_Is_Valid())
            {
                /* 选项字节异常但APP带有有效密钥时，允许JLink开发烧录后按向量表启动 */
                Option_Bytes_Set_BOOT_State(BOOT_STATE_JUMP_APP);
                IAP_DEBUG("BOOT_STATE_JLINK_APP\r\n");
            }
            else
            {
                /* 选项字节出错且APP无效，保持错误状态等待重新升级 */
                Option_Bytes_Set_APP_KEY(0x00000000);
                Option_Bytes_Set_BOOT_State(BOOT_STATE_APP_ERROR);
                Option_Bytes_Set_APP_SIZE(0x00000000);
                Option_Bytes_Set_APP_CRC32(0xFFFFFFFF);
                Option_Bytes_Send_Data();
                IAP_DEBUG("BOOT_STATE_APP_ERROR\r\n");
            }
            break;

        }
    }
}

/* 烧录前准备 */
uint8_t IAP_Flash_Init(void)
{
    uint8_t Error = 0;
    IAP_Write_Offset = 0;
    /* 清除选项代码，断电后不会误跳转到半包APP */
    Option_Bytes_Set_APP_KEY(0x00000000);
    /* 升级过程中固定保持APP_UPDATA，防止擦写半包后被向量表兜底逻辑误跳转 */
    Option_Bytes_Set_BOOT_State(BOOT_STATE_APP_UPDATA);
    Option_Bytes_Set_APP_SIZE(0x00000000);
    Option_Bytes_Set_APP_CRC32(0x00000000);
    Option_Bytes_Clear_File_Info();
    Option_Bytes_Send_Data();
    /* 烧录前擦除APP区域，不擦除BootLoader和选项字节 */
    FLASH_UnlockAllPages();
    Error = FLASH_ErasePages(APP_START_ADDR, APP_FLASH_END_ADDR - 1);
    FLASH_LockAllPages();
    return Error;
}

/* IAP程序编程 */
uint8_t IAP_Flash_Proc(uint32_t Packet_Number, uint8_t XYmodem_Mode, uint8_t *Data, uint16_t Len)
{
    uint8_t Error = 0;
    (void)Packet_Number;
    (void)XYmodem_Mode;
    if (Data == 0 || Len == 0)
        return 0xFF;
    if (IAP_Write_Offset > IAP_APP_MAX_SIZE || Len > (IAP_APP_MAX_SIZE - IAP_Write_Offset))
        return 0xFE;

    /* 写入Flash数据 */
    FLASH_UnlockAllPages();
    Error = FLASH_WriteBytes(APP_START_ADDR + IAP_Write_Offset, Data, Len);
    FLASH_LockAllPages();
    if (Error != 0)
        return Error;

    IAP_Write_Offset += Len;
    Option_Bytes_Set_APP_SIZE(IAP_Write_Offset);
    Option_Bytes_Set_APP_CRC32(Data_CRC32_Calculate(Option_Bytes_Get_APP_CRC32(), Data, Len));
    /* 分包写入期间保持升级状态，只有IAP_Flash_DeInit校验通过后才允许跳转APP */
    Option_Bytes_Set_BOOT_State(BOOT_STATE_APP_UPDATA);
    return Error;
}

/* 烧录完成 */
void IAP_Flash_DeInit(void)
{
    if (IAP_APP_Is_Valid(0))
    {
        /* APP升级完成 */
        Option_Bytes_Set_APP_KEY(APP_READY_KEY);
        Option_Bytes_Set_BOOT_State(BOOT_STATE_APP_UPDATA_READY);
    }
    else
    {
        /* APP校验失败，保持错误状态等待重新升级 */
        Option_Bytes_Set_APP_KEY(0x00000000);
        Option_Bytes_Set_BOOT_State(BOOT_STATE_APP_ERROR);
    }
    Option_Bytes_Send_Data();
    IAP_DEBUG("IAP_Flash_DeInit\r\n");
    IAP_DEBUG("APP_KEY \t: %#x\r\n", Option_Bytes_Get_APP_KEY());
    IAP_DEBUG("APP_SIZE\t: %#x\r\n", Option_Bytes_Get_APP_SIZE());
    IAP_DEBUG("APP_CRC32\t: %#x\r\n", Option_Bytes_Get_APP_CRC32());
    IAP_DEBUG("OB_CRC32\t: %#x\r\n", Option_Bytes_Get_OB_CRC32());
    IAP_DEBUG("FILE_PROTOCOL\t: %#x\r\n", Option_Bytes_Get_File_Protocol());
    IAP_DEBUG("FILE_SIZE\t: %#x\r\n", Option_Bytes_Get_File_Size());
}

/* 烧录完成跳转到APP */
void IAP_Jump_To_APP(void)
{
    uint32_t JumpAddress = 0;
    typedef void (*func_ptr_t)(void);
    func_ptr_t Jump_To_Application = 0;

    Option_Bytes_Read_Data();
    IAP_DEBUG("Get Option Bytes\r\n");

    /* 判断APP栈顶、复位向量、密钥、大小和CRC32是否合法 */
    if (IAP_APP_Is_Valid(1) || (Option_Bytes_Get_APP_KEY() == APP_READY_KEY && IAP_APP_Vector_Is_Valid()))
    {
        IAP_DEBUG("IAP_Jump_To_APP\r\n");
        Option_Bytes_Set_BOOT_State(BOOT_STATE_APP);
        Option_Bytes_Send_Data();

        /* 更新APP数据完成后使用硬件复位 */
        if (APP_UPDATA_READY)
            NVIC_SystemReset();
        else
        {
            /* 跳转前关闭中断和BootLoader使用的外设 */
            __disable_irq();
            Uart_DeInit();
            System_Tick_DeInit();

            SCB->VTOR = APP_START_ADDR;
            JumpAddress = *(__IO uint32_t *) (APP_START_ADDR + 4);
            Jump_To_Application = (func_ptr_t)JumpAddress;
            __set_MSP(*(__IO uint32_t *)APP_START_ADDR);
            Jump_To_Application();
        }
    }
    else
    {
        Option_Bytes_Set_APP_KEY(0x00000000);
        Option_Bytes_Set_BOOT_State(BOOT_STATE_APP_ERROR);
        Option_Bytes_Send_Data();
    }
}
#endif
