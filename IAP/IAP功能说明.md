# IAP功能说明

本文档按当前 `index.html` 最新程序整理，说明网页版上位机的 IAP 操作流程、通信协议、XYmodem 下载规则，以及 `IAP/示例代码` 中下位机示例代码的移植使用方法。

## 一、适用范围

1. 上位机页面：`index.html` 中的 `更新程序` 界面。
2. 下位机示例：`IAP/示例代码` 目录中的 `iap.*`、`iap_cmd.*`、`xymodem.*`、`option_bytes.*`、`flash_port.*`、`crc.*`。
3. 当前上位机支持 `Xmodem - 128`、`Xmodem - 1K`、`Ymodem - 128`、`Ymodem - 1K` 四种传输协议，默认协议为 `Ymodem - 1K`。
4. 当前上位机的校验模式由设备请求自动识别：设备发送字符 `C(0x43)` 时使用 `CRC-16校验`，设备发送 `NAK(0x15)` 时使用 `和校验`。

## 二、IAP界面进入和退出

1. 在数据监测界面连续点击标题 `充电宝数据监测` 5 次，进入开发者模式。
2. 登录管理员账号后，在 `USB_CMD_GET_TEST / SET_TEST` 区域下方显示 `更新程序` 按钮。
3. 点击 `更新程序` 后进入独立 IAP 界面，普通数据轮询会停止，避免普通串口命令和 IAP 下载流互相干扰。
4. IAP 界面提供两个退出入口：
   - `返回数据界面`：退出 IAP 并回到普通数据监测界面。
   - `返回开发者模式`：退出 IAP 并回到开发者调试界面。

## 三、IAP界面显示内容

1. 连接状态：
   - 刚进入 IAP 界面时显示 `等待 GET_ACK`。
   - 设备返回 `IAP_CMD_GET_ACK` 且数据为 `0x01` 后，认为 IAP 通道连接正常。
   - option bytes 读取完成后显示当前 `BOOT_State`。
2. 文件信息：
   - `目录`：浏览器可提供真实路径时显示真实目录；普通网页环境受安全策略限制时显示目录受限提示。
   - `文件名`：显示用户选择的 `.bin` 文件名。
   - `原始大小`：显示 `.bin` 文件原始字节数。
   - `传输大小`：显示当前协议下实际参与传输和 CRC-32 计算的大小。
   - `CRC-32`：显示当前传输数据计算得到的 CRC-32。
3. option bytes 信息：
   - `BOOT_State`：显示设备当前引导状态。
   - `APP_SIZE`：显示设备保存的 APP 镜像大小。
   - `APP_CRC32`：显示设备保存的 APP 镜像 CRC-32。
   - `协议`：显示设备保存的文件传输协议。
   - `文件信息`：显示 Ymodem 文件名和原始文件大小。
   - `读取次数`：显示本次 IAP 界面读取 option bytes 的次数。
4. 下载信息：
   - `进度`：显示已经确认发送的数据比例。
   - `剩余字节`：显示待传输或待确认的数据大小。
   - `状态`：显示当前下载阶段、失败原因或完成校验结果。
   - `IAP日志`：显示 GET_ACK、GET_OPTION_BYTES、BOOT_State 切换、XYmodem 包发送、ACK/NAK/CAN 等关键过程。

## 四、IAP命令帧格式

上位机和下位机的 IAP 控制命令使用独立帧格式，和 XYmodem 文件流分开处理。

```text
Header0 | Header1 | CMD   | Len      | Data | CRC8
0xAA    | 0x55    | 1字节 | 2字节LE  | Len  | 1字节
```

1. `Len` 为小端格式，低字节在前，高字节在后。
2. `CRC8` 使用 CRC8-CCITT，初值为 `0xFF`。
3. `CRC8` 计算范围为 `CMD + Len低字节 + Len高字节 + Data`，不包含 `0xAA 0x55` 帧头。
4. 当前上位机接收 IAP 回包时允许 `Data <= 128`，示例下位机接收上位机控制命令时 `Data <= 32`。当前三个命令足够使用；如果后续新增大数据控制命令，需要同步调整上下位机长度限制。

### 1. IAP_CMD_GET_ACK(0x00)

1. 上位机发送空数据命令，用于判断设备是否进入 IAP 通道。
2. 下位机正常响应 `Data[0] = 0x01`。
3. 进入 IAP 界面后，上位机每 `1000ms` 发送一次 `IAP_CMD_GET_ACK`，直到收到正常响应后停止。

### 2. IAP_CMD_GET_OPTION_BYTES(0x01)

1. 上位机发送空数据命令，用于读取设备 option bytes。
2. 下位机返回 option bytes 原始数据，当前示例布局如下：

```text
Offset  Size  字段
0       4     APP_Key
4       4     BOOT_State
8       4     APP_SIZE
12      4     APP_CRC32
16      4     OB_CRC32
20      4     File_Protocol
24      4     File_Size
28      4     File_Name_Len
32      64    File_Name
```

3. 上位机至少需要收到前 16 字节，才能显示 `BOOT_State`、`APP_SIZE`、`APP_CRC32`。
4. 如果回包长度达到 20 字节以上，上位机会继续解析 `OB_CRC32`。
5. 如果回包长度达到 32 字节以上，上位机会继续解析文件协议、原始文件大小和文件名。
6. `File_Name` 只按 ASCII 可显示字符解析，非法字符会按 `_` 显示。

### 3. IAP_CMD_SET_BOOT_STATE(0x02)

1. 上位机发送 1 字节目标状态。
2. APP 状态下只接受 `BOOT_STATE_APP_REQUEST_UPGRAD`，用于请求复位进入 BootLoader。
3. BootLoader 状态下只接受 `BOOT_STATE_APP_UPDATA` 和 `BOOT_STATE_JUMP_APP`。
4. 下位机正常响应 `Data[0] = 0x01`，参数不合法或状态不允许时响应 `Data[0] = 0x00`。
5. 上位机设置状态后会重新读取 option bytes 或等待 BootLoader 发送 XYmodem 请求，避免复用上一轮状态。

## 五、文件选择和传输大小规则

1. 文件选择只允许 `.bin` 文件。
2. 文件大小为 0 时直接拒绝，避免 Ymodem 空数据包导致设备端状态异常。
3. 当前上位机限制 APP 镜像最大传输大小为 `0x9E00` 字节，对应示例代码中的 `APP_START_ADDR = 0x00006000`、`APP_OPTION_BYTES_ADDR = 0x0000FE00`。
4. `Xmodem - 128`：
   - 数据区为 128 字节。
   - 最后一包不足 128 字节时使用 `Ctrl-Z(0x1A)` 补齐。
   - `APP_SIZE` 和 `APP_CRC32` 按补齐后的传输数据计算。
5. `Xmodem - 1K`：
   - 数据区为 1024 字节。
   - 最后一包不足 1024 字节时使用 `Ctrl-Z(0x1A)` 补齐。
   - `APP_SIZE` 和 `APP_CRC32` 按补齐后的传输数据计算。
6. `Ymodem - 128` / `Ymodem - 1K`：
   - 先发送 0 号文件信息包，包含文件名和原始文件大小。
   - 数据包仍按 128 字节或 1024 字节对齐发送。
   - 下位机写 Flash 时只写入原始文件大小范围内的数据，最后一包补齐字节不会写入 APP 区。
   - `APP_SIZE` 和 `APP_CRC32` 按原始文件数据计算。
7. Ymodem 文件名最长保存 64 字节；上位机会过滤 `\ / : * ? " < > |` 等不适合放入文件信息包的字符。

## 六、IAP下载流程

1. 进入 IAP 界面后，上位机停止普通数据轮询，并开始发送 `IAP_CMD_GET_ACK`。
2. `GET_ACK` 确认成功后，上位机每 `1000ms` 发送 `IAP_CMD_GET_OPTION_BYTES`，直到读取到有效 option bytes。
3. 用户选择 `.bin` 文件并确认传输协议后，点击 `下载`。
4. 上位机先检查：
   - 是否已选择文件。
   - 传输大小是否超过 `0x9E00`。
   - 串口是否已经连接。
5. 下载前上位机重新读取一次 option bytes，防止使用旧状态。
6. 如果设备当前处于 `BOOT_STATE_APP`：
   - 上位机发送 `BOOT_STATE_APP_REQUEST_UPGRAD`。
   - APP 示例代码收到后设置 `System.Jump_BootLoader = 1`。
   - APP 周期处理函数写入 option bytes 并触发 `NVIC_SystemReset()`。
   - 上位机等待 BootLoader 进入下载窗口。
7. 如果设备尚未处于 `BOOT_STATE_APP_UPDATA`：
   - 上位机发送 `BOOT_STATE_APP_UPDATA`。
   - BootLoader 示例代码复位 XYmodem 状态机，并等待文件数据。
8. 当设备进入 `BOOT_STATE_APP_UPDATA` 后，BootLoader 会周期性发送数据请求：
   - `C(0x43)`：请求 CRC-16 校验数据包。
   - `NAK(0x15)`：请求和校验数据包。
9. 上位机收到请求后自动确定校验模式，并开始发送 XYmodem 数据。
10. 数据包格式：

```text
SOH/STX | PacketNo | ~PacketNo | Data | Check
```

11. `SOH(0x01)` 表示 128 字节数据包，`STX(0x02)` 表示 1024 字节数据包。
12. `Check` 根据设备请求自动选择：
    - CRC-16：2 字节，高字节在前，算法为 Xmodem CRC16，初值 `0x0000`，多项式 `0x1021`。
    - 和校验：1 字节，为数据区所有字节累加后取低 8 位。
13. 设备返回 `ACK(0x06)` 后，上位机延时 `80ms` 发送下一包。
14. 设备返回 `NAK(0x15)` 或等待 `ACK / NAK / CAN` 超过 `5s` 时，上位机重发当前阶段的数据包；同一阶段连续失败达到 5 次后判定下载失败。
15. 设备返回 `CAN(0x18)` 时，上位机立即取消下载并提示失败。
16. Xmodem 数据发送完成后，上位机发送 `EOT(0x04)`，然后进入完成校验。
17. Ymodem 数据发送完成后，流程为：
    - 上位机发送 `EOT(0x04)`。
    - 设备返回 `ACK` 后继续请求 `C` 或 `NAK`。
    - 上位机发送 Ymodem 结束空包。
    - 设备返回 `ACK` 后进入完成校验。
18. 下载完成后，上位机延时约 `800ms`，最多重试 5 次读取 option bytes。
19. 上位机将本地计算的 `APP_SIZE / APP_CRC32` 与设备返回值比较：
    - 全部一致时显示 `下载完成，APP_SIZE / APP_CRC32 校验通过`。
    - CRC 一致但大小不同，会提示大小不同。
    - CRC 不一致，会提示 `APP_CRC32 与本地计算值不一致`。

## 七、测试演示按钮

1. `测试演示` 不发送 IAP 控制命令，也不发送 XYmodem 文件流。
2. 未选择文件时，页面会自动生成 `iap-demo.bin` 测试文件。
3. 测试演示只用于验证文件信息、传输大小、CRC-32、进度条和剩余字节显示效果。
4. 真实升级必须使用 `下载` 按钮，并连接已经适配 IAP 示例代码或等效协议的设备。

## 八、IAP示例代码文件说明

`IAP/示例代码` 目录提供一套可移植的下位机 IAP 参考实现，默认按 APP 工程和 BootLoader 工程共用同一套源码的方式组织。

| 文件 | 作用 |
| --- | --- |
| `iap.h` / `iap.c` | IAP 总控逻辑，负责 APP 合法性校验、BOOT_State 状态机、APP 请求升级、BootLoader 擦写 APP、升级完成后跳转 APP。 |
| `iap_cmd.h` / `iap_cmd.c` | IAP 控制命令解析和响应，负责 `GET_ACK`、`GET_OPTION_BYTES`、`SET_BOOT_STATE`，并在 BootLoader 升级状态下把串口数据转交给 `XYmodem_Read()`。 |
| `xymodem.h` / `xymodem.c` | Xmodem/Ymodem 接收状态机，负责 `C/NAK/ACK/CAN/EOT` 控制流、128/1K 数据包拼包、CRC16/和校验、Ymodem 文件信息包解析。 |
| `option_bytes.h` / `option_bytes.c` | option bytes 的 RAM 镜像、Flash 读写、APP_KEY、BOOT_State、APP_SIZE、APP_CRC32、OB_CRC32、文件协议、文件大小和文件名维护。 |
| `flash_port.h` / `flash_port.c` | Flash 擦除和写入适配层，需要按实际 MCU HAL 修改。 |
| `crc.h` / `crc.c` | CRC8-CCITT、Xmodem CRC16、标准 CRC32 计算函数。 |

## 九、示例代码移植步骤

1. 将 `IAP/示例代码` 中的 `.c/.h` 文件加入 APP 工程和 BootLoader 工程。
2. 根据工程类型定义编译宏：
   - APP 工程定义 `TD66S_T_APP` 或 `CW32L910_APP`。
   - BootLoader 工程定义 `TD66S_T_BOOTLOADER` 或 `CW32L910_BOOTLOADER`。
3. 在 `iap.h` 中确认 Flash 地址：
   - `APP_START_ADDR`：APP 起始地址。
   - `APP_OPTION_BYTES_ADDR`：option bytes 存放地址。
   - `APP_FLASH_END_ADDR`：APP 区结束地址，当前默认等于 option bytes 起始地址。
   - `IAP_APP_MAX_SIZE`：APP 最大可写大小。
   - `IAP_SRAM_START_ADDR / IAP_SRAM_END_ADDR`：用于判断 APP 栈顶是否合法。
4. 在 `flash_port.c` 中适配实际芯片 Flash HAL：
   - `FLASH_UnlockAllPages()`。
   - `FLASH_LockAllPages()`。
   - `FLASH_ErasePages()`。
   - `FLASH_WriteBytes()`。
   - `FLASH_WriteWords()`。
5. 在串口接收回调或串口数据处理入口调用：

```c
IAP_CMD_Receive_Data_Pack(Data, Len);
```

6. 在系统初始化完成后调用：

```c
IAP_Init();
```

7. 在主循环或固定周期任务中调用：

```c
IAP_Proc();
```

8. APP 工程中的使用重点：
   - `IAP_Init()` 会读取 option bytes，并把异常状态恢复为 `BOOT_STATE_APP`。
   - 当上位机发送 `BOOT_STATE_APP_REQUEST_UPGRAD` 后，`iap_cmd.c` 会设置 `System.Jump_BootLoader = 1`。
   - `IAP_Proc()` 检测到 `System.Jump_BootLoader == 1` 后，写入 `BOOT_STATE_APP_REQUEST_UPGRAD` 并执行 `NVIC_SystemReset()`。
9. BootLoader 工程中的使用重点：
   - `IAP_Init()` 读取 option bytes 后，`IAP_Proc()` 根据 `BOOT_State` 决定跳转 APP、进入升级窗口或等待重新升级。
   - `BOOT_STATE_APP_UPDATA` 状态下，`IAP_Proc()` 周期调用 `XYmodem_Proc()`，设备会定时发送 `C` 请求数据。
   - `IAP_CMD_Receive_Data_Pack()` 在 BootLoader 升级状态下会把串口数据转给 `XYmodem_Read()`。
   - 第一包有效数据到达时，`IAP_Flash_Init()` 会擦除 APP 区并清除旧 APP 校验信息。
   - 每个有效数据包写入 Flash 后，`IAP_Flash_Proc()` 会累加更新 `APP_SIZE` 和 `APP_CRC32`。
   - 传输结束后，`IAP_Flash_DeInit()` 校验 APP，有效则写入 `APP_READY_KEY` 并切换到 `BOOT_STATE_APP_UPDATA_READY`。
10. 如果需要支持 JLink 直接烧录 APP 后启动，可按工程需要打开 `OPTION_BYTE_APP_KEY_ENABLE`，让 APP 镜像带上初始 option bytes；量产 IAP 流程建议由 BootLoader 维护 option bytes。

## 十、示例代码依赖项

移植前需要在目标工程中提供或替换以下依赖：

1. `system.h` 中的基础类型、`System` 全局状态和 `System_Check_Time()`。
2. `uart.h` 中的 `Uart_Transmit()`、`Uart_Printf()`、`Uart_DeInit()`。
3. `NVIC_SystemReset()`、`__disable_irq()`、`__set_MSP()`、`SCB->VTOR` 等 Cortex-M 相关接口。
4. `System_Tick_DeInit()`，用于跳转 APP 前关闭 BootLoader 使用的节拍。
5. Flash HAL 接口和宏：
   - `HAL_FLASH_Erase()`。
   - `HAL_FLASH_Program()`。
   - `FLASH_TYPEERASE_PAGES`。
   - `FLASH_TYPEPROGRAM_BYTE`。
   - `FLASH_TYPEPROGRAM_WORD`。
   - `FLASH_PAGE_SIZE`。
   - `FLASH_SIZE_64K` 或目标芯片实际 Flash 总大小。

## 十一、option bytes维护规则

1. 上电后必须先调用 `Option_Bytes_Read_Data()`，把 Flash 中的 option bytes 复制到 RAM 变量。
2. 业务代码只能通过 `Option_Bytes_Get_...()` 和 `Option_Bytes_Set_...()` 访问 option bytes，不建议直接 extern 内部变量。
3. 每次修改 APP_KEY、BOOT_State、APP_SIZE、APP_CRC32 或文件信息后，示例代码都会重新计算 `OB_CRC32`。
4. `OB_CRC32` 自身不参与 option bytes CRC 计算，计算范围为前 16 字节和 `Offset 20` 之后的文件信息区。
5. 退出 BootLoader 或完成状态切换前，必须调用 `Option_Bytes_Send_Data()`，把 RAM 中的 option bytes 写回 Flash。
6. 升级开始时会清除 `APP_KEY`，避免断电或半包升级后误跳转到损坏 APP。
7. 升级完成且 APP 向量、大小、CRC32 校验全部通过后，才重新写入 `APP_READY_KEY`。

## 十二、验证建议

1. 使用 Chrome 或 Edge 打开页面，浏览器必须支持 Web Serial API。
2. 连接设备后进入 IAP 界面，确认日志中先出现 `IAP_CMD_GET_ACK`，随后出现 `GET_ACK 响应为 1`。
3. 确认 option bytes 能正常显示 `BOOT_State`、`APP_SIZE`、`APP_CRC32`。
4. 选择 `.bin` 文件后，确认原始大小、传输大小和 CRC-32 是否符合所选协议。
5. 默认使用 `Ymodem - 1K` 进行下载，确认设备先收到 Ymodem 文件信息包，再收到数据包。
6. 下载完成后确认上位机显示 `APP_SIZE / APP_CRC32 校验通过`。
7. 断电重启设备，确认 BootLoader 能根据 `APP_READY_KEY`、APP 向量表、`APP_SIZE` 和 `APP_CRC32` 跳转到 APP。
8. 如果下载过程中失败，优先查看 IAP 日志中的 `NAK`、`CAN`、超时次数、BOOT_State 和完成校验结果。

## 十三、注意事项

1. IAP 下载期间不要在开发者透传区发送普通串口数据，避免和 XYmodem 文件流混在一起。
2. `APP_START_ADDR`、`APP_OPTION_BYTES_ADDR`、链接脚本和中断向量表偏移必须一致，否则 BootLoader 校验通过也可能无法正确跳转 APP。
3. `APP_OPTION_BYTES_ADDR` 所在页不要被 APP 镜像覆盖，也不要被普通参数存储复用。
4. 如果实际芯片不是 64K Flash，需要同步修改 `flash_port.c` 中的地址范围检查和 `iap.h` 中的地址规划。
5. 如果设备端固定 `XYMODEM_CRC_ENABLE == 1`，BootLoader 空闲时会固定发送 `C`，上位机会自动使用 CRC-16 校验。
6. 如果要改成和校验，需要同步确认设备请求码、上位机自动识别逻辑和包长度判断规则。
7. 浏览器网页无法稳定获取用户选择文件的真实绝对路径，页面只显示运行环境允许读取到的目录信息。
