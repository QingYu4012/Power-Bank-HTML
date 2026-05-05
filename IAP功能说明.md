# IAP功能说明

## IAP进入 / 退出流程
1. 再数据界面点击 5 次 `充电宝数据监测` 后进入开发者界面
2. 登录管理员账号
3. 点击 `更新程序按钮` 进入 `更新程序界面`
    - 登录管理员身份后再 `USB_CMD_GET_TEST / SET_TEST` 命令下方新增 `更新程序` 按钮
    - 再更新程序界面下面需要添加两个按钮，1：`返回数据界面` 2：`返回开发者模式`
4. 通过点击 `返回数据界面` 或 `返回开发者模式` 按钮退出 `更新程序界面`

## IAP界面功能
1. 再IAP界面要求需要主动读取设备的 `option bytes` ，并且将 `option bytes` 的 `APP_SIZE` 和 `APP_CRC32` 显示出来
   - 进入更新程序界面后，每1S送一次 `IAP_CMD_GET_OPTION_BYTES` 命令，知道设备返回 `option bytes`后停止
2. `传输协议` 下拉列表选择框，下载程序时需要按照 `传输协议` 选择的协议进行传输下载的文件
   -  下拉列表选择框有四个选择，1： `Xmodem - 128`  2： `Xmodem - 1K`  3： `Ymodem - 128`  4： `Ymodem - 1K` 
   -  默认选择 `Xmodem - 1K` 传输协议
3. `校验模式` 勾选框，下载程序时需要按照 `校验模式` 选择的模式进行传输下载的文件
   -  勾选框有两个选择，1： `和校验`  2： `CRC-16校验` 
   -  默认选择 `CRC-16校验` 传输协议
4. `选择文件` 按钮，点击按钮后，打开文件夹用户选择文件
   - 用户选择文件只能是 `.bin` 类型的文件
   - 用户选择文件后需要显示文件所在的目录
   - 用户选择文件后需要显示文件名字
   - 用户选择文件后需要显示文件大小
      - 需要区分传输协议，因为 `Xmodem` 再文件不足 `128` 或 `1K` 字节时，会用 `Ctrl-Z(0x1A)` 补满 `128` 或 `1K` 字节，因此大小的长度必须是`128` 或 `1K` 的整数倍，不足 `128` 或 `1K` 字节时补足`128` 或 `1K` 字节，使用 `Ymodem` 协议时因为存在文件属性数据包，因此保持默认的大小
   - 用户选择文件后需要对文件进行 `CRC-32校验` 并显示
      - 需要区分传输协议，因为 `Xmodem` 再文件不足 `128` 或 `1K` 字节时，会用 `Ctrl-Z(0x1A)` 补满 `128` 或 `1K` 字节，因此计算 `CRC-32校验` 时需要包括使用 `Ctrl-Z(0x1A)` 补满  `128` 或 `1K` 字节的数据，使用 `Ymodem` 协议时因为存在文件属性数据包，因此保持默认的大小
5. `下载` 按钮，用户点击下载按钮进入下载流程，通过上面选择的 `传输协议` `校验模式` `选择文件`，进行下载
   - 下载失败时需要提示下载失败，
   - 下载超时时需要提示下载超时，
   - 设备未连接时需要提示设备未连接
   - 用户未选择文件时需要提示文件未选择
   - 点击下载后需要显示 `进度条` 和 `剩余字节`

## IAP流程
1. 通过 `GET_ACK` 命令判断设备是否正确连接
   - 进入 `IAP界面` 后每1S发送一次 `GET_ACK`，直到获取到响应后停止
2. 通过 `GET_OPTION_BYTES` 命令获取 `选项字节`，在进行下载前必须 `读取第一次选项字节`
   -每1S读取一次 `选项字节`，直到获取到选择字节后停止
3. 等待用户 `选择文件`和 `下载格式 ` 并点击 `下载按钮` 后执行下面流程
4. 当 `BOOT_State` 为 `BOOT_STATE_APP` 时切换到 `BOOT_STATE_APP_REQUEST_UPGRAD` 
   - 当 `BOOT_State` 不为 `BOOT_STATE_APP` 时说明设备正在执行 `BootLoader` 程序，允许通过命令进行 `APP程序升级`
5. 当前设备进入到 `BOOT_STATE_APP_REQUEST_UPGRAD` 状态后，设置 `BootLoader` 状态为 `BOOT_STATE_APP_UPDATA` 
   - 设备在 `APP损坏` 或者 `出现异常` 时会主动进入 `BOOT_STATE_APP_UPDATA` 
6. 当前设备进入到 `BOOT_STATE_APP_UPDATA` 状态后，设备会定时发送 `0x15` 或字符 `C` 请求数据发送
   - 当未发送 `第一包文件数据` 前，允许通过 `BOOT_STATE_JUMP_APP` 命令重新回到 `APP程序`
   - 当未发送 `第一包文件数据` 后，设备会直接擦除所有的 `APP程序`，因此不允许通过 `BOOT_STATE_JUMP_APP` 命令重新回到 `APP程序`
   - 设备擦除所有的 `APP程序` 的同时会擦除 `APP_KEY` `APP_SIZE` `APP_CRC32`，因此上位机发送 `BOOT_STATE_JUMP_APP` 命令后设备会自动回到 `BOOT_STATE_APP_UPDATA` 状态
7. 当检测到已经处于 `BOOT_STATE_APP_UPDATA` 状态，即收到多个 `0x15` 或字符 `C` 请求数据发送命令时，允许不再发送命令重新进入 `BOOT_STATE_APP_UPDATA` 状态，直接发送 `文件数据`
8. 正式下载步骤：
   - 设备每 `500mS` 发送一次 `0x15` 或字符 `C` 命令，接收到对应校验模式的命令后发送第一包数据
      - `CRC-16校验` 校验历程
      ```C
      // CRC16-CCITT 标准多项式: x^16 + x^12 + x^5 + 1
      // MSB-first 形式: 0x1021 (省略最高位x^16)
      #define CRC16_POLYNOMIAL_CCITT  0x1021
      #define CRC16_XOR_OUT           0x0000

      /**
      * @brief CRC16逐位计算（无查找表，内存占用极小）
      * @param  Init_Value  初始CRC值（CCITT常用0x0000或0xFFFF）
      * @param  Data        指向数据缓冲区的指针
      * @param  Len         数据长度（字节数）
      * @return             最终CRC16校验值
      */
      uint16_t Data_CRC16_Verify(uint16_t Init_Value, const uint8_t *Data, uint32_t Len)
      {
         uint16_t crc = Init_Value;
         uint32_t i;
         int j;
         
         for (i = 0; i < Len; i++) {
            crc ^= ((uint16_t)Data[i] << 8);  // 字节移到高8位，与CRC高字节异或
            
            for (j = 0; j < 8; j++) {  // 逐位处理（MSB-first）
                  if (crc & 0x8000) {    // 检查最高位
                     crc = (crc << 1) ^ CRC16_POLYNOMIAL_CCITT;
                  } else {
                     crc <<= 1;
                  }
            }
         }
         
         return crc ^ CRC16_XOR_OUT;
      }
      ```
   - 发送数据包后设置 `5S超时时间` 5S内未收到 `ACK` /  `NAK` /  `CAN` 后重新发送该数据包， `5次超时` 后下载失败
   - 收到 `ACK` /  `NAK` 数据包后 `500mS` 后发送 下一帧数据
9. 文件下载完成后重新读取设备 `选项字节` ，判断 `APP_CRC32` 是否通过
   - 文件下载完成后设备会主动跳转执行  `APP程序`
注意：
   - 设置状态后，需要重新执行 `第二点步骤` 用于判断是否正常切换状态
