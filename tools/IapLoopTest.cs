using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.Ports;
using System.Text;
using System.Threading;

namespace H20IapLoopTest
{
    internal sealed class ProtocolCase
    {
        public string Name;
        public int BlockSize;
        public bool IsYmodem;
        public uint OptionProtocol;

        public ProtocolCase(string name, int blockSize, bool isYmodem, uint optionProtocol)
        {
            Name = name;
            BlockSize = blockSize;
            IsYmodem = isYmodem;
            OptionProtocol = optionProtocol;
        }
    }

    internal sealed class OptionInfo
    {
        public uint AppKey;
        public uint BootState;
        public uint AppSize;
        public uint AppCapacity;
        public uint AppCrc32;
        public uint ObCrc32;
        public uint FileProtocol;
        public uint FileSize;
        public string FileName = string.Empty;
    }

    internal sealed class Logger : IDisposable
    {
        private readonly StreamWriter writer;

        public Logger(string path)
        {
            if (!string.IsNullOrEmpty(path))
            {
                writer = new StreamWriter(path, false, new UTF8Encoding(false));
                writer.AutoFlush = true;
            }
        }

        public void Write(string message)
        {
            string line = string.Format(
                CultureInfo.InvariantCulture,
                "[{0:yyyy-MM-dd HH:mm:ss.fff}] {1}",
                DateTime.Now,
                message);
            Console.WriteLine(line);
            if (writer != null)
                writer.WriteLine(line);
        }

        public void Dispose()
        {
            if (writer != null)
                writer.Dispose();
        }
    }

    internal sealed class SerialLink : IDisposable
    {
        private readonly SerialPort port;
        private readonly List<byte> receiveBuffer = new List<byte>(65536);
        private readonly Logger log;

        public SerialLink(string portName, int baudRate, Logger logger)
        {
            log = logger;
            port = new SerialPort(portName, baudRate, Parity.None, 8, StopBits.One);
            port.Handshake = Handshake.None;
            port.ReadTimeout = 20;
            port.WriteTimeout = 5000;
            port.ReadBufferSize = 65536;
            port.WriteBufferSize = 65536;
            port.DtrEnable = false;
            port.RtsEnable = false;
        }

        public void Open()
        {
            port.Open();
            port.DiscardInBuffer();
            port.DiscardOutBuffer();
            receiveBuffer.Clear();
        }

        public void ClearInput()
        {
            Pump();
            receiveBuffer.Clear();
            port.DiscardInBuffer();
        }

        public void Write(byte[] data)
        {
            if (data == null || data.Length == 0)
                throw new ArgumentException("发送数据不能为空");
            Write(data, 0, data.Length);
        }

        /* 支持故障注入时从指定偏移发送，正常发送仍统一走同一串口写入入口。 */
        public void Write(byte[] data, int offset, int count)
        {
            if (data == null || count <= 0 || offset < 0 || offset + count > data.Length)
                throw new ArgumentException("发送数据范围非法");
            port.Write(data, offset, count);
        }

        public byte[] RequestIap(byte command, byte[] payload, int timeoutMs)
        {
            ClearInput();
            Write(BuildIapFrame(command, payload));
            DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
            while (DateTime.UtcNow < deadline)
            {
                Pump();
                byte[] response;
                if (TryTakeIapFrame(command, out response))
                    return response;
                Thread.Sleep(2);
            }
            return null;
        }

        public byte WaitControl(byte[] accepted, int timeoutMs)
        {
            DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
            while (DateTime.UtcNow < deadline)
            {
                Pump();
                for (int i = 0; i < receiveBuffer.Count; i++)
                {
                    byte value = receiveBuffer[i];
                    for (int j = 0; j < accepted.Length; j++)
                    {
                        if (value != accepted[j])
                            continue;
                        receiveBuffer.RemoveRange(0, i + 1);
                        return value;
                    }
                }
                if (receiveBuffer.Count > 4096)
                    receiveBuffer.RemoveRange(0, receiveBuffer.Count - 128);
                Thread.Sleep(1);
            }
            throw new TimeoutException("等待设备控制字符超时");
        }

        /* 在首个应答后继续等待短暂静默，以最后一个控制字符关联本次发送并清除重复应答。 */
        public byte SettleControl(
            byte initialControl,
            byte[] accepted,
            int quietMs,
            out int additionalCount)
        {
            byte latest = initialControl;
            DateTime quietDeadline = DateTime.UtcNow.AddMilliseconds(quietMs);
            additionalCount = 0;
            while (DateTime.UtcNow < quietDeadline)
            {
                int remaining = (int)Math.Ceiling(
                    (quietDeadline - DateTime.UtcNow).TotalMilliseconds);
                if (remaining <= 0)
                    break;
                try
                {
                    latest = WaitControl(accepted, remaining);
                    additionalCount++;
                    quietDeadline = DateTime.UtcNow.AddMilliseconds(quietMs);
                }
                catch (TimeoutException)
                {
                    break;
                }
            }
            return latest;
        }

        private void Pump()
        {
            while (port.BytesToRead > 0)
            {
                int count = Math.Min(port.BytesToRead, 4096);
                byte[] data = new byte[count];
                int read = port.Read(data, 0, data.Length);
                for (int i = 0; i < read; i++)
                    receiveBuffer.Add(data[i]);
            }
        }

        private bool TryTakeIapFrame(byte expectedCommand, out byte[] payload)
        {
            payload = null;
            int index = 0;
            while (index + 6 <= receiveBuffer.Count)
            {
                if (receiveBuffer[index] != 0xAA || receiveBuffer[index + 1] != 0x55)
                {
                    index++;
                    continue;
                }

                byte command = receiveBuffer[index + 2];
                int length = receiveBuffer[index + 3] | (receiveBuffer[index + 4] << 8);
                if (length > 128)
                {
                    index++;
                    continue;
                }

                int frameLength = 6 + length;
                if (index + frameLength > receiveBuffer.Count)
                {
                    if (index > 0)
                        receiveBuffer.RemoveRange(0, index);
                    return false;
                }

                byte[] checkData = new byte[3 + length];
                for (int i = 0; i < checkData.Length; i++)
                    checkData[i] = receiveBuffer[index + 2 + i];
                byte receivedCrc = receiveBuffer[index + frameLength - 1];
                bool ackCompatible = command == 0x00 &&
                                     length == 1 &&
                                     receiveBuffer[index + 5] == 0x01 &&
                                     receivedCrc == 0x2F;
                if (Crc8(checkData) != receivedCrc && !ackCompatible)
                {
                    index++;
                    continue;
                }

                byte[] framePayload = new byte[length];
                for (int i = 0; i < length; i++)
                    framePayload[i] = receiveBuffer[index + 5 + i];
                receiveBuffer.RemoveRange(0, index + frameLength);
                index = 0;
                if ((command & 0x7F) == (expectedCommand & 0x7F))
                {
                    payload = framePayload;
                    return true;
                }
            }

            if (index > 0)
                receiveBuffer.RemoveRange(0, index);
            return false;
        }

        private static byte[] BuildIapFrame(byte command, byte[] payload)
        {
            byte[] data = payload ?? new byte[0];
            if (data.Length > 128)
                throw new ArgumentException("IAP数据超过128字节");
            byte[] frame = new byte[6 + data.Length];
            frame[0] = 0xAA;
            frame[1] = 0x55;
            frame[2] = command;
            frame[3] = (byte)(data.Length & 0xFF);
            frame[4] = (byte)((data.Length >> 8) & 0xFF);
            Array.Copy(data, 0, frame, 5, data.Length);
            byte[] checkData = new byte[3 + data.Length];
            Array.Copy(frame, 2, checkData, 0, checkData.Length);
            frame[frame.Length - 1] = Crc8(checkData);
            return frame;
        }

        private static byte Crc8(byte[] data)
        {
            int crc = 0xFF;
            for (int i = 0; i < data.Length; i++)
            {
                crc ^= data[i];
                for (int bit = 0; bit < 8; bit++)
                    crc = (crc & 0x80) != 0 ? ((crc << 1) ^ 0x07) & 0xFF : (crc << 1) & 0xFF;
            }
            return (byte)crc;
        }

        public void Dispose()
        {
            try
            {
                if (port.IsOpen)
                    port.Close();
            }
            finally
            {
                port.Dispose();
            }
        }
    }

    internal static class Program
    {
        private const byte IapGetAck = 0x00;
        private const byte IapGetOption = 0x01;
        private const byte IapSetBootState = 0x02;
        private const byte IapSetTransferInfo = 0x03;
        private const uint AppReadyKey = 0xAA55BBDD;
        private const uint BootReady = 0;
        private const uint BootAppError = 1;
        private const uint BootAppRequestUpgrade = 2;
        private const uint BootAppUpdate = 3;
        private const uint BootAppUpdateReady = 4;
        private const uint BootJumpApp = 5;
        private const uint BootApp = 6;
        private const byte Soh = 0x01;
        private const byte Stx = 0x02;
        private const byte Eot = 0x04;
        private const byte Ack = 0x06;
        private const byte Nak = 0x15;
        private const byte Can = 0x18;
        private const byte CrcRequest = 0x43;
        private const int MaxRetries = 10;

        private static readonly ProtocolCase[] Cases =
        {
            new ProtocolCase("Xmodem-128", 128, false, 1),
            new ProtocolCase("Xmodem-1K", 1024, false, 2),
            new ProtocolCase("Ymodem-128", 128, true, 3),
            new ProtocolCase("Ymodem-1K", 1024, true, 4)
        };

        private static Logger log;

        public static int Main(string[] args)
        {
            Console.OutputEncoding = Encoding.UTF8;
            string imagePath;
            string portName;
            string logPath;
            string protocolName;
            int baudRate;
            int loops;
            int injectDropStxPacket;
            bool faultSuite;
            if (!ParseArguments(
                    args,
                    out imagePath,
                    out portName,
                    out baudRate,
                    out loops,
                    out logPath,
                    out protocolName,
                    out injectDropStxPacket,
                    out faultSuite))
                return 2;

            byte[] image = File.ReadAllBytes(imagePath);
            if (image.Length == 0)
            {
                Console.Error.WriteLine("镜像大小非法：{0} 字节", image.Length);
                return 2;
            }

            ProtocolCase[] selectedCases = SelectCases(protocolName);
            /* 完整故障套件固定覆盖四种协议，避免遗漏Xmodem提前EOT或任一Ymodem块长。 */
            if (faultSuite && selectedCases.Length != Cases.Length)
            {
                Console.Error.WriteLine("--fault-suite 必须与 --protocol all 配合使用");
                return 2;
            }
            /* 丢STX测试只允许选择一个1K协议，避免参数生效范围含糊。 */
            if (injectDropStxPacket > 0 &&
                (selectedCases.Length != 1 || selectedCases[0].BlockSize != 1024))
            {
                Console.Error.WriteLine("--inject-drop-stx-packet 仅可与单个1K协议配合使用");
                return 2;
            }
            if (injectDropStxPacket > 0)
            {
                int packetCount = (image.Length + selectedCases[0].BlockSize - 1) /
                                  selectedCases[0].BlockSize;
                if (injectDropStxPacket > packetCount)
                {
                    Console.Error.WriteLine(
                        "故障注入包号{0}超过数据包总数{1}",
                        injectDropStxPacket,
                        packetCount);
                    return 2;
                }
            }

            using (log = new Logger(logPath))
            {
                log.Write(string.Format(
                    CultureInfo.InvariantCulture,
                    "开始测试：PORT={0} BAUD={1} IMAGE={2} SIZE={3} LOOPS={4} PROTOCOL={5} INJECT_DROP_STX_PACKET={6} FAULT_SUITE={7}",
                    portName,
                    baudRate,
                    Path.GetFullPath(imagePath),
                    image.Length,
                    loops,
                    protocolName,
                    injectDropStxPacket,
                    faultSuite));
                try
                {
                    using (SerialLink link = new SerialLink(portName, baudRate, log))
                    {
                        link.Open();
                        if (faultSuite)
                        {
                            RunInboundCanBurstCancellation(link, image);
                            log.Write("PASS 入站8字节CAN突发取消与会话复位");
                            Thread.Sleep(350);
                            RunEarlyEotRejection(
                                link,
                                image);
                            log.Write("PASS Xmodem-128提前EOT拒绝与双CAN取消");
                            Thread.Sleep(350);
                        }
                        int passed = 0;
                        int total = selectedCases.Length * loops;
                        for (int caseIndex = 0; caseIndex < selectedCases.Length; caseIndex++)
                        {
                            ProtocolCase protocol = selectedCases[caseIndex];
                            for (int loop = 1; loop <= loops; loop++)
                            {
                                log.Write(string.Format(
                                    CultureInfo.InvariantCulture,
                                    "[{0}/{1}] {2} 第{3}/{4}次开始",
                                    passed + 1,
                                    total,
                                    protocol.Name,
                                    loop,
                                    loops));
                                RunUpgrade(
                                    link,
                                    protocol,
                                    image,
                                    Path.GetFileName(imagePath),
                                    injectDropStxPacket,
                                    faultSuite);
                                passed++;
                                log.Write(string.Format(
                                    CultureInfo.InvariantCulture,
                                    "PASS {0} 第{1}/{2}次，累计{3}/{4}",
                                    protocol.Name,
                                    loop,
                                    loops,
                                    passed,
                                    total));
                                Thread.Sleep(350);
                            }
                        }
                        if (selectedCases.Length == Cases.Length)
                            log.Write(string.Format(
                                CultureInfo.InvariantCulture,
                                "ALL PASS：四种协议各连续{0}次，共{1}次升级全部成功",
                                loops,
                                total));
                        else
                            log.Write(string.Format(
                                CultureInfo.InvariantCulture,
                                "ALL PASS：{0}连续{1}次，共{2}次升级全部成功",
                                selectedCases[0].Name,
                                loops,
                                total));
                    }
                    return 0;
                }
                catch (Exception ex)
                {
                    log.Write("FAIL：" + ex);
                    return 1;
                }
            }
        }

        private static void RunUpgrade(
            SerialLink link,
            ProtocolCase protocol,
            byte[] image,
            string fileName,
            int injectDropStxPacket,
            bool faultSuite)
        {
            EnsureAck(link);
            OptionInfo before = ReadOption(link, 3);
            byte[] transferData = protocol.IsYmodem ? Clone(image) : PadImage(image, protocol.BlockSize);
            uint expectedCrc = Crc32(transferData);
            uint expectedSize = (uint)transferData.Length;
            if (before.AppCapacity < 8)
                throw new InvalidDataException("设备Option Bytes未提供有效APP容量，请先使用J-Link更新BootLoader");
            if (expectedSize > before.AppCapacity)
                throw new InvalidDataException(string.Format(
                    CultureInfo.InvariantCulture,
                    "镜像超过设备APP容量：{0} > {1}",
                    expectedSize,
                    before.AppCapacity));
            log.Write(string.Format(
                CultureInfo.InvariantCulture,
                "设备APP容量={0}，本次传输={1}",
                before.AppCapacity,
                expectedSize));

            EnterUpdateMode(link, before);
            SetTransferInfo(link, expectedSize, expectedCrc);
            byte request = link.WaitControl(new byte[] { CrcRequest, Nak, Can }, 12000);
            if (request == Can)
                throw new IOException("设备在握手阶段发送CAN");
            if (request != CrcRequest)
                throw new IOException("设备未请求CRC16模式");

            byte[] ymodemHeader = null;
            if (protocol.IsYmodem)
            {
                ymodemHeader = BuildYmodemHeader(protocol, fileName, image.Length, false);
                if (faultSuite)
                    TestYmodemHeaderRecovery(link, protocol, ymodemHeader, image.Length);
                else
                {
                    SendPacketWithRetry(link, ymodemHeader, "Ymodem文件信息包", false);
                    WaitYmodemDataRequest(link);
                }
            }

            int packetCount = (transferData.Length + protocol.BlockSize - 1) / protocol.BlockSize;
            for (int packetIndex = 0; packetIndex < packetCount; packetIndex++)
            {
                byte[] packet = BuildDataPacket(
                    protocol.BlockSize,
                    packetIndex + 1,
                    transferData,
                    packetIndex * protocol.BlockSize);
                SendPacketWithRetry(link, packet, string.Format(
                    CultureInfo.InvariantCulture,
                    "数据包{0}/{1}",
                    packetIndex + 1,
                    packetCount),
                    packetIndex + 1 == injectDropStxPacket);
                /* 与网页上位机保持相同的包间隔，给CH341接收ACK和主循环切换状态留出余量。 */
                Thread.Sleep(80);
                if ((packetIndex + 1) % 32 == 0 || packetIndex + 1 == packetCount)
                    log.Write(string.Format(
                        CultureInfo.InvariantCulture,
                        "{0} 数据进度 {1}/{2}",
                        protocol.Name,
                        packetIndex + 1,
                        packetCount));
            }

            if (protocol.IsYmodem)
                FinishYmodem(link, protocol, ymodemHeader, faultSuite);
            else
                FinishXmodem(link, faultSuite);

            OptionInfo after = WaitForCompletedOption(link, expectedSize, expectedCrc);
            if (after.AppKey != AppReadyKey ||
                after.AppSize != expectedSize ||
                after.AppCrc32 != expectedCrc ||
                !IsCompletedState(after.BootState))
            {
                throw new InvalidDataException(string.Format(
                    CultureInfo.InvariantCulture,
                    "完成校验失败 KEY=0x{0:X8} STATE={1} SIZE={2}/{3} CRC=0x{4:X8}/0x{5:X8}",
                    after.AppKey,
                    after.BootState,
                    after.AppSize,
                    expectedSize,
                    after.AppCrc32,
                    expectedCrc));
            }

            if (after.FileProtocol != 0 && after.FileProtocol != protocol.OptionProtocol)
                throw new InvalidDataException("设备记录的文件协议与发送协议不一致");
            if (protocol.IsYmodem && after.FileSize != 0 && after.FileSize != image.Length)
                throw new InvalidDataException("设备记录的Ymodem原始文件大小不一致");
        }

        /* 传输运行中一次发送8个CAN，随后必须回到空闲态并允许重新设置清单。 */
        private static void RunInboundCanBurstCancellation(SerialLink link, byte[] image)
        {
            ProtocolCase protocol = Cases[0];
            byte[] transferData = PadImage(image, protocol.BlockSize);
            uint expectedSize = (uint)transferData.Length;
            uint expectedCrc = Crc32(transferData);

            EnsureAck(link);
            OptionInfo before = ReadOption(link, 3);
            EnterUpdateMode(link, before);
            SetTransferInfo(link, expectedSize, expectedCrc);
            byte request = link.WaitControl(new byte[] { CrcRequest, Nak, Can }, 12000);
            if (request != CrcRequest)
                throw new IOException("入站CAN测试未收到CRC16请求");

            byte[] firstPacket = BuildDataPacket(
                protocol.BlockSize,
                1,
                transferData,
                0);
            SendPacketWithRetry(link, firstPacket, "入站CAN测试首包", false);
            link.ClearInput();
            byte[] cancel = new byte[8];
            for (int i = 0; i < cancel.Length; i++)
                cancel[i] = Can;
            link.Write(cancel);
            Thread.Sleep(100);

            /* 只有CAN突发已复位XYmodem和旧清单时，新清单才会被设备接受。 */
            SetTransferInfo(link, expectedSize, expectedCrc);
        }

        /* 仅发送一个Xmodem-128数据包后提前EOT，必须拒绝完成并连续返回两个CAN。 */
        private static void RunEarlyEotRejection(SerialLink link, byte[] image)
        {
            ProtocolCase protocol = Cases[0];
            byte[] transferData = PadImage(image, protocol.BlockSize);
            uint expectedSize = (uint)transferData.Length;
            uint expectedCrc = Crc32(transferData);

            EnsureAck(link);
            OptionInfo before = ReadOption(link, 3);
            EnterUpdateMode(link, before);
            SetTransferInfo(link, expectedSize, expectedCrc);
            byte request = link.WaitControl(new byte[] { CrcRequest, Nak, Can }, 12000);
            if (request != CrcRequest)
                throw new IOException("提前EOT测试未收到CRC16请求");

            byte[] firstPacket = BuildDataPacket(
                protocol.BlockSize,
                1,
                transferData,
                0);
            SendPacketWithRetry(link, firstPacket, "提前EOT测试首包", false);
            Thread.Sleep(80);
            link.ClearInput();
            link.Write(new byte[] { Eot });
            byte firstCan = link.WaitControl(new byte[] { Can, Ack, Nak }, 3000);
            if (firstCan != Can)
                throw new IOException("Xmodem提前EOT未以CAN拒绝");
            byte secondCan = link.WaitControl(new byte[] { Can }, 500);
            if (secondCan != Can)
                throw new IOException("Xmodem提前EOT未连续发送双CAN");

            OptionInfo rejected = ReadOption(link, 3);
            if (rejected.AppKey == AppReadyKey || IsCompletedState(rejected.BootState))
                throw new InvalidDataException("Xmodem提前EOT被错误标记为升级完成");
        }

        private static void EnterUpdateMode(SerialLink link, OptionInfo option)
        {
            if (option.BootState == BootApp)
            {
                SetBootState(link, BootAppRequestUpgrade);
                Thread.Sleep(1500);
                link.ClearInput();
                return;
            }

            if (option.BootState != BootAppUpdate)
            {
                SetBootState(link, BootAppUpdate);
                Thread.Sleep(350);
            }
            link.ClearInput();
        }

        private static void EnsureAck(SerialLink link)
        {
            for (int attempt = 0; attempt < 5; attempt++)
            {
                byte[] payload = link.RequestIap(IapGetAck, new byte[0], 1800);
                if (payload != null && payload.Length >= 1 && payload[0] == 1)
                    return;
                Thread.Sleep(250);
            }
            throw new IOException("GET_ACK连续5次无有效响应");
        }

        private static void SetBootState(SerialLink link, uint state)
        {
            byte[] payload = link.RequestIap(
                IapSetBootState,
                new byte[] { (byte)state },
                2200);
            if (payload == null || payload.Length == 0 || payload[0] != 1)
                throw new IOException("设置BOOT_State失败：" + state.ToString(CultureInfo.InvariantCulture));
        }

        /* 文件流开始前独立下发长度和CRC32，设备据此拒绝提前EOT或截断镜像。 */
        private static void SetTransferInfo(SerialLink link, uint size, uint crc)
        {
            byte[] data = new byte[8];
            WriteUInt32(data, 0, size);
            WriteUInt32(data, 4, crc);
            byte[] payload = link.RequestIap(IapSetTransferInfo, data, 2200);
            if (payload == null || payload.Length == 0 || payload[0] != 1)
                throw new IOException("设备拒绝升级长度/CRC32清单");
            log.Write(string.Format(
                CultureInfo.InvariantCulture,
                "升级清单已确认 SIZE={0} CRC32=0x{1:X8}",
                size,
                crc));
        }

        private static OptionInfo ReadOption(SerialLink link, int retries)
        {
            for (int attempt = 0; attempt < retries; attempt++)
            {
                byte[] payload = link.RequestIap(IapGetOption, new byte[0], 2200);
                OptionInfo option;
                if (TryParseOption(payload, out option))
                    return option;
                Thread.Sleep(250);
            }
            throw new IOException("GET_OPTION_BYTES无有效响应");
        }

        private static OptionInfo WaitForCompletedOption(SerialLink link, uint size, uint crc)
        {
            Exception lastError = null;
            for (int attempt = 0; attempt < 20; attempt++)
            {
                try
                {
                    OptionInfo option = ReadOption(link, 1);
                    if (option.AppKey == AppReadyKey &&
                        option.AppSize == size &&
                        option.AppCrc32 == crc &&
                        IsCompletedState(option.BootState))
                        return option;
                    lastError = new InvalidDataException(string.Format(
                        CultureInfo.InvariantCulture,
                        "状态尚未完成：KEY=0x{0:X8} STATE={1} SIZE={2} CRC=0x{3:X8}",
                        option.AppKey,
                        option.BootState,
                        option.AppSize,
                        option.AppCrc32));
                }
                catch (Exception ex)
                {
                    lastError = ex;
                }
                Thread.Sleep(250);
            }
            throw new IOException("升级后option bytes校验超时", lastError);
        }

        private static bool TryParseOption(byte[] data, out OptionInfo option)
        {
            option = null;
            if (data == null || data.Length < 16)
                return false;
            OptionInfo parsed = new OptionInfo();
            parsed.AppKey = ReadUInt32(data, 0);
            parsed.BootState = ReadUInt32(data, 4);
            parsed.AppSize = ReadUInt32(data, 8);
            parsed.AppCrc32 = ReadUInt32(data, 12);
            parsed.ObCrc32 = data.Length >= 20 ? ReadUInt32(data, 16) : 0;
            parsed.FileProtocol = data.Length >= 24 ? ReadUInt32(data, 20) : 0;
            parsed.FileSize = data.Length >= 28 ? ReadUInt32(data, 24) : 0;
            parsed.AppCapacity = data.Length >= 100 ? ReadUInt32(data, 96) : 0;
            uint nameLength = data.Length >= 32 ? ReadUInt32(data, 28) : 0;
            if (nameLength > 64)
                nameLength = 64;
            if (32 + nameLength > data.Length)
                nameLength = (uint)Math.Max(0, data.Length - 32);
            if (nameLength > 0)
                parsed.FileName = Encoding.ASCII.GetString(data, 32, (int)nameLength);
            option = parsed;
            return parsed.BootState <= 6;
        }

        private static void SendPacketWithRetry(
            SerialLink link,
            byte[] packet,
            string label,
            bool dropHeaderOnFirstAttempt)
        {
            bool injectedNakReceived = !dropHeaderOnFirstAttempt;
            for (int retry = 0; retry < MaxRetries; retry++)
            {
                bool injectThisAttempt = dropHeaderOnFirstAttempt && retry == 0;
                Stopwatch watch = Stopwatch.StartNew();
                if (injectThisAttempt)
                {
                    if (packet.Length < 2 || packet[0] != Stx)
                        throw new InvalidOperationException(label + "不是可注入的STX数据包");
                    /* 首次故意少发STX，确定性复现UART丢包头后的接收恢复路径。 */
                    log.Write(string.Format(
                        CultureInfo.InvariantCulture,
                        "{0} 第{1}/{2}次发送：注入丢失STX",
                        label,
                        retry + 1,
                        MaxRetries));
                    link.Write(packet, 1, packet.Length - 1);
                }
                else
                {
                    if (retry > 0)
                        log.Write(string.Format(
                            CultureInfo.InvariantCulture,
                            "{0} 第{1}/{2}次发送：完整重发",
                            label,
                            retry + 1,
                            MaxRetries));
                    link.Write(packet);
                }
                byte control;
                try
                {
                    control = link.WaitControl(new byte[] { Ack, Nak, Can }, 5000);
                }
                catch (TimeoutException)
                {
                    log.Write(string.Format(
                        CultureInfo.InvariantCulture,
                        "{0} 第{1}/{2}次等待超时，耗时{3}ms",
                        label,
                        retry + 1,
                        MaxRetries,
                        watch.ElapsedMilliseconds));
                    /* 保留串口缓冲，并给临界时刻到达的ACK留出宽限期，避免主动清空有效应答。 */
                    try
                    {
                        control = link.WaitControl(new byte[] { Ack, Nak, Can }, 250);
                        log.Write(string.Format(
                            CultureInfo.InvariantCulture,
                            "{0} 在超时宽限期收到{1}，总耗时{2}ms",
                            label,
                            ControlName(control),
                            watch.ElapsedMilliseconds));
                    }
                    catch (TimeoutException)
                    {
                        if (retry + 1 >= MaxRetries)
                            throw new TimeoutException(label + "等待ACK超时");
                        Thread.Sleep(80);
                        continue;
                    }
                }
                byte firstControl = control;
                int additionalControls;
                control = link.SettleControl(
                    control,
                    new byte[] { Ack, Nak, Can },
                    20,
                    out additionalControls);
                if (additionalControls > 0)
                    log.Write(string.Format(
                        CultureInfo.InvariantCulture,
                        "{0} 应答突发共{1}个：首个{2}，最终{3}",
                        label,
                        additionalControls + 1,
                        ControlName(firstControl),
                        ControlName(control)));
                watch.Stop();
                if (control == Ack)
                {
                    if (!injectedNakReceived)
                        throw new IOException(label + "故障注入后未收到NAK");
                    if (retry > 0)
                        log.Write(string.Format(
                            CultureInfo.InvariantCulture,
                            "{0} 第{1}/{2}次重发收到ACK，耗时{3}ms",
                            label,
                            retry + 1,
                            MaxRetries,
                            watch.ElapsedMilliseconds));
                    return;
                }
                if (control == Can)
                    throw new IOException(label + "收到CAN");
                log.Write(string.Format(
                    CultureInfo.InvariantCulture,
                    "{0} 第{1}/{2}次收到NAK，耗时{3}ms",
                    label,
                    retry + 1,
                    MaxRetries,
                    watch.ElapsedMilliseconds));
                if (injectThisAttempt)
                    injectedNakReceived = true;
                if (retry + 1 >= MaxRetries)
                    throw new IOException(label + "连续收到NAK");
                Thread.Sleep(80);
            }
        }

        /* 将控制字符转换为稳定的诊断文本。 */
        private static string ControlName(byte control)
        {
            if (control == Ack)
                return "ACK";
            if (control == Nak)
                return "NAK";
            if (control == Can)
                return "CAN";
            return string.Format(CultureInfo.InvariantCulture, "0x{0:X2}", control);
        }

        /* 等待Ymodem头包后的数据阶段CRC16请求。 */
        private static void WaitYmodemDataRequest(SerialLink link)
        {
            byte request = link.WaitControl(new byte[] { CrcRequest, Nak, Can }, 5000);
            if (request == Can)
                throw new IOException("设备取消Ymodem数据阶段");
            if (request != CrcRequest)
                throw new IOException("Ymodem文件信息包后未收到C");
        }

        /* 翻转包尾CRC并确认设备明确NAK，随后由调用方发送正确包恢复。 */
        private static void SendCorruptedPacketAndExpectNak(
            SerialLink link,
            byte[] packet,
            string label)
        {
            byte[] corrupted = Clone(packet);
            corrupted[corrupted.Length - 1] ^= 0x01;
            link.ClearInput();
            link.Write(corrupted);
            byte control = link.WaitControl(new byte[] { Ack, Nak, Can }, 5000);
            if (control != Nak)
                throw new IOException(label + "CRC故障未收到NAK");
            log.Write(label + "翻转CRC后收到NAK，阶段保持有效");
        }

        /* 覆盖Ymodem初始头CRC错误及ACK/C被消费后的同头幂等重发。 */
        private static void TestYmodemHeaderRecovery(
            SerialLink link,
            ProtocolCase protocol,
            byte[] header,
            int fileSize)
        {
            SendCorruptedPacketAndExpectNak(link, header, protocol.Name + "文件信息包");
            SendPacketWithRetry(link, header, protocol.Name + "文件信息包首次正确发送", false);
            WaitYmodemDataRequest(link);
            log.Write(protocol.Name + "故意消费首次文件信息包ACK/C，开始同头重发");

            link.ClearInput();
            SendPacketWithRetry(link, header, protocol.Name + "文件信息包幂等重发", false);
            WaitYmodemDataRequest(link);
            OptionInfo option = ReadOption(link, 3);
            int nameEnd = Array.IndexOf(header, (byte)0, 3, protocol.BlockSize);
            if (nameEnd < 3)
                throw new InvalidDataException("Ymodem文件信息包缺少文件名结束符");
            string expectedName = Encoding.ASCII.GetString(
                header,
                3,
                nameEnd - 3);
            if (option.FileProtocol != protocol.OptionProtocol ||
                option.FileSize != (uint)fileSize ||
                option.FileName != expectedName)
                throw new InvalidDataException("Ymodem文件信息包幂等重发后元数据不一致");
            log.Write(protocol.Name + "文件信息包CRC恢复及ACK/C丢失重答通过");
        }

        /* 每隔5.2秒主动清空旧应答并重发结束帧，验证完成窗口按有效ACK持续刷新。 */
        private static void VerifyCompletionWindow(
            SerialLink link,
            byte[] completionPacket,
            string label)
        {
            for (int retry = 1; retry <= 2; retry++)
            {
                Thread.Sleep(5200);
                link.ClearInput();
                Stopwatch watch = Stopwatch.StartNew();
                link.Write(completionPacket);
                byte control = link.WaitControl(new byte[] { Ack, Nak, Can }, 5000);
                watch.Stop();
                if (control != Ack)
                    throw new IOException(label + "完成窗口重答未收到ACK");
                log.Write(string.Format(
                    CultureInfo.InvariantCulture,
                    "{0} 第{1}/2次在5.2秒后重发收到ACK，耗时{2}ms",
                    label,
                    retry,
                    watch.ElapsedMilliseconds));
            }
        }

        private static void FinishXmodem(SerialLink link, bool faultSuite)
        {
            /* Xmodem结束帧与普通数据包共用迟到ACK和重复EOT恢复策略。 */
            byte[] eot = new byte[] { Eot };
            SendPacketWithRetry(link, eot, "Xmodem EOT", false);
            if (faultSuite)
                VerifyCompletionWindow(link, eot, "Xmodem EOT");
        }

        private static void FinishYmodem(
            SerialLink link,
            ProtocolCase protocol,
            byte[] initialHeader,
            bool faultSuite)
        {
            bool firstStageCompleted = false;
            bool finalEotAcked = false;
            for (int retry = 0; retry < MaxRetries; retry++)
            {
                link.Write(new byte[] { Eot });
                byte first;
                try
                {
                    first = link.WaitControl(new byte[] { Nak, Ack, Can }, 5000);
                }
                catch (TimeoutException)
                {
                    log.Write(string.Format(
                        CultureInfo.InvariantCulture,
                        "Ymodem首次EOT第{0}/{1}次等待超时，准备重发",
                        retry + 1,
                        MaxRetries));
                    continue;
                }
                if (first == Can)
                    throw new IOException("Ymodem首次EOT收到CAN");
                if (first == Ack)
                    finalEotAcked = true;
                firstStageCompleted = true;
                break;
            }
            if (!firstStageCompleted)
                throw new TimeoutException("Ymodem首次EOT连续等待超时");

            if (!finalEotAcked)
            {
                /* 第二个EOT允许NAK、ACK丢失后重复发送，设备端会幂等返回ACK和C。 */
                SendPacketWithRetry(link, new byte[] { Eot }, "Ymodem第二次EOT", false);
            }

            byte request = 0;
            bool finalHeaderRequested = false;
            for (int retry = 0; retry < MaxRetries; retry++)
            {
                try
                {
                    request = link.WaitControl(new byte[] { CrcRequest, Nak, Can }, 5000);
                }
                catch (TimeoutException)
                {
                    log.Write(string.Format(
                        CultureInfo.InvariantCulture,
                        "Ymodem等待结束空包请求C第{0}/{1}次超时",
                        retry + 1,
                        MaxRetries));
                    continue;
                }
                if (request == Can)
                    throw new IOException("Ymodem结束阶段收到CAN");
                if (request == CrcRequest)
                {
                    finalHeaderRequested = true;
                    break;
                }
            }
            if (!finalHeaderRequested)
                throw new TimeoutException("Ymodem结束阶段未收到C");
            byte[] finalPacket = BuildYmodemHeader(protocol, string.Empty, 0, true);
            if (faultSuite)
                SendCorruptedPacketAndExpectNak(
                    link,
                    finalPacket,
                    protocol.Name + "结束空包");
            SendPacketWithRetry(link, finalPacket, "Ymodem结束空包", false);
            if (faultSuite)
            {
                link.ClearInput();
                link.Write(initialHeader);
                byte lateHeaderControl = link.WaitControl(
                    new byte[] { Ack, Nak, Can },
                    5000);
                if (lateHeaderControl != Nak)
                    throw new IOException(protocol.Name + "完成后迟到旧头包未被NAK拒绝");
                log.Write(protocol.Name + "完成后迟到旧头包已拒绝，APP保持完成状态");
                VerifyCompletionWindow(link, finalPacket, protocol.Name + "结束空包");
            }
        }

        private static byte[] BuildYmodemHeader(
            ProtocolCase protocol,
            string fileName,
            int fileSize,
            bool finalPacket)
        {
            byte[] payload = new byte[protocol.BlockSize];
            if (!finalPacket)
            {
                string safeName = string.IsNullOrEmpty(fileName) ? "H20_APP.bin" : fileName;
                byte[] name = Encoding.ASCII.GetBytes(safeName);
                if (name.Length > 64)
                    Array.Resize(ref name, 64);
                byte[] size = Encoding.ASCII.GetBytes(fileSize.ToString(CultureInfo.InvariantCulture));
                if (name.Length + size.Length + 2 > payload.Length)
                    throw new InvalidDataException("Ymodem文件信息超过包容量");
                Array.Copy(name, 0, payload, 0, name.Length);
                payload[name.Length] = 0;
                Array.Copy(size, 0, payload, name.Length + 1, size.Length);
            }
            return BuildPacket(protocol.BlockSize, 0, payload);
        }

        private static byte[] BuildDataPacket(
            int blockSize,
            int packetNumber,
            byte[] source,
            int offset)
        {
            byte[] payload = new byte[blockSize];
            for (int i = 0; i < payload.Length; i++)
                payload[i] = 0x1A;
            int copy = Math.Min(blockSize, source.Length - offset);
            if (copy > 0)
                Array.Copy(source, offset, payload, 0, copy);
            return BuildPacket(blockSize, packetNumber, payload);
        }

        private static byte[] BuildPacket(int blockSize, int packetNumber, byte[] payload)
        {
            byte[] packet = new byte[3 + blockSize + 2];
            byte sequence = (byte)(packetNumber & 0xFF);
            packet[0] = blockSize == 128 ? Soh : Stx;
            packet[1] = sequence;
            packet[2] = (byte)~sequence;
            Array.Copy(payload, 0, packet, 3, blockSize);
            ushort crc = Crc16(payload);
            packet[3 + blockSize] = (byte)((crc >> 8) & 0xFF);
            packet[4 + blockSize] = (byte)(crc & 0xFF);
            return packet;
        }

        private static byte[] PadImage(byte[] image, int blockSize)
        {
            int length = ((image.Length + blockSize - 1) / blockSize) * blockSize;
            byte[] padded = new byte[length];
            for (int i = 0; i < padded.Length; i++)
                padded[i] = 0x1A;
            Array.Copy(image, padded, image.Length);
            return padded;
        }

        private static byte[] Clone(byte[] data)
        {
            byte[] result = new byte[data.Length];
            Array.Copy(data, result, data.Length);
            return result;
        }

        private static ushort Crc16(byte[] data)
        {
            int crc = 0;
            for (int i = 0; i < data.Length; i++)
            {
                crc ^= data[i] << 8;
                for (int bit = 0; bit < 8; bit++)
                    crc = (crc & 0x8000) != 0 ? ((crc << 1) ^ 0x1021) & 0xFFFF : (crc << 1) & 0xFFFF;
            }
            return (ushort)crc;
        }

        private static uint Crc32(byte[] data)
        {
            uint crc = 0xFFFFFFFF;
            for (int i = 0; i < data.Length; i++)
            {
                crc ^= data[i];
                for (int bit = 0; bit < 8; bit++)
                    crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xEDB88320U : crc >> 1;
            }
            return crc ^ 0xFFFFFFFF;
        }

        private static uint ReadUInt32(byte[] data, int offset)
        {
            return (uint)(data[offset] |
                          (data[offset + 1] << 8) |
                          (data[offset + 2] << 16) |
                          (data[offset + 3] << 24));
        }

        /* 按IAP命令约定写入小端32位字段。 */
        private static void WriteUInt32(byte[] data, int offset, uint value)
        {
            data[offset] = (byte)(value & 0xFF);
            data[offset + 1] = (byte)((value >> 8) & 0xFF);
            data[offset + 2] = (byte)((value >> 16) & 0xFF);
            data[offset + 3] = (byte)((value >> 24) & 0xFF);
        }

        private static bool IsCompletedState(uint state)
        {
            return state == BootAppUpdateReady ||
                   state == BootReady ||
                   state == BootJumpApp ||
                   state == BootApp;
        }

        private static bool ParseArguments(
            string[] args,
            out string imagePath,
            out string portName,
            out int baudRate,
            out int loops,
            out string logPath,
            out string protocolName,
            out int injectDropStxPacket,
            out bool faultSuite)
        {
            imagePath = null;
            portName = "COM7";
            baudRate = 1500000;
            loops = 10;
            logPath = null;
            protocolName = "all";
            injectDropStxPacket = 0;
            faultSuite = false;
            if (args.Length == 0)
            {
                PrintUsage();
                return false;
            }
            imagePath = args[0];
            for (int i = 1; i < args.Length; i++)
            {
                if (args[i] == "--port" && i + 1 < args.Length)
                    portName = args[++i];
                else if (args[i] == "--baud" && i + 1 < args.Length)
                    baudRate = int.Parse(args[++i], CultureInfo.InvariantCulture);
                else if (args[i] == "--loops" && i + 1 < args.Length)
                    loops = int.Parse(args[++i], CultureInfo.InvariantCulture);
                else if (args[i] == "--log" && i + 1 < args.Length)
                    logPath = args[++i];
                else if (args[i] == "--protocol" && i + 1 < args.Length)
                    protocolName = args[++i];
                else if (args[i] == "--inject-drop-stx-packet" && i + 1 < args.Length)
                    injectDropStxPacket = int.Parse(args[++i], CultureInfo.InvariantCulture);
                else if (args[i] == "--fault-suite")
                    faultSuite = true;
                else
                {
                    Console.Error.WriteLine("未知或不完整参数：{0}", args[i]);
                    PrintUsage();
                    return false;
                }
            }
            if (!File.Exists(imagePath))
            {
                Console.Error.WriteLine("镜像不存在：{0}", imagePath);
                return false;
            }
            if (baudRate <= 0 || loops <= 0 || injectDropStxPacket < 0)
            {
                Console.Error.WriteLine("波特率、循环次数必须大于0，故障注入包号不能小于0");
                return false;
            }
            if (SelectCases(protocolName).Length == 0)
            {
                Console.Error.WriteLine("不支持的协议：{0}", protocolName);
                return false;
            }
            return true;
        }

        /* 按命令行参数筛选协议，all表示执行完整四协议矩阵。 */
        private static ProtocolCase[] SelectCases(string protocolName)
        {
            if (string.Equals(protocolName, "all", StringComparison.OrdinalIgnoreCase))
                return Cases;
            for (int i = 0; i < Cases.Length; i++)
            {
                if (string.Equals(Cases[i].Name, protocolName, StringComparison.OrdinalIgnoreCase))
                    return new ProtocolCase[] { Cases[i] };
            }
            return new ProtocolCase[0];
        }

        private static void PrintUsage()
        {
            Console.WriteLine(
                "用法：IapLoopTest.exe <H20_APP.bin> [--port COM7] [--baud 1500000] [--loops 10] [--protocol all|xmodem-128|xmodem-1k|ymodem-128|ymodem-1k] [--inject-drop-stx-packet N] [--fault-suite] [--log result.log]");
        }
    }
}
