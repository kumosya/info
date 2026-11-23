/*
 * 初始化键盘鼠标控制器，读取带来源信息的输入字节。
 */

#include "kernel/device.h"
#include "kernel/sync.h"

namespace device {

    /*
     * 从 PS/2 使用的 I/O 端口读取一个字节，作为控制器底层访问入口。
     */
    static int readPs2Port(uint16_t port, uint8_t& value) {
        asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));

        return 0;
    }

    /*
     * 向 PS/2 使用的 I/O 端口写入一个字节，作为控制器命令和数据发送入口。
     */
    static int writePs2Port(uint16_t port, uint8_t value) {
        asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));

        return 0;
    }

    /* 封装 PS/2 命令应答与鼠标模式协商，保留控制器配置和数据包长度。 */
    class Controller {
        public:
            /*
             * 清理旧数据、暂时关闭端口并更新控制器配置，再启用键盘与鼠标；尝试协商滚轮模式后启动数据上报。
             */
            bool init() {
                // 丢弃最多三十二个旧字节，防止上次操作残留的数据被当作本轮命令回复。
                uint8_t ignored;

                for (uint32_t index = 0; index < 32 && outputReady(); ++index) {
                    readPs2Port(0x60, ignored);
                }

                // 暂时关闭两个端口再读取配置，避免设备上报与配置回复混在一起。
                if (!writeCommand(0xad) || !writeCommand(0xa7) ||
                    !writeCommand(0x20) || !readData(configuration_)) {
                    return false;
                }

                // 打开两个端口的中断允许位并取消时钟禁用，写回配置后再重新开启端口。
                configuration_ =
                    static_cast<uint8_t>((configuration_ | 0x03U) & ~0x30U);

                if (!writeCommand(0x60) || !writeData(configuration_) ||
                    !writeCommand(0xae) || !writeCommand(0xa8)) {
                    return false;
                }

                // 滚轮协商失败仍能使用普通三字节鼠标包，最后才启用键盘和鼠标持续上报。
                mousePacketSize_ = enableMouseWheel() ? 4 : 3;

                return sendKeyboard(0xf4) && sendMouse(0xf4);
            }

            /*
             * 只在输出缓冲区有数据时取一个字节，并根据状态位区分键盘还是鼠标来源，不在中断路径长时间等待。
             */
            bool readInterruptByte(uint8_t& value, bool& mouse) {
                uint8_t status;

                if (readPs2Port(0x64, status) != 0 || (status & 1U) == 0 ||
                    readPs2Port(0x60, value) != 0) {
                    return false;
                }

                mouse = (status & 0x20U) != 0;

                return true;
            }

            /*
             * 返回协商得到的鼠标数据包长度，普通鼠标为三字节，滚轮模式为四字节。
             */
            uint8_t mousePacketSize() const { return mousePacketSize_; }

        private:
            /*
             * 有限次轮询控制器输入缓冲区是否空闲，允许发送下一条命令；超时返回失败。
             */
            bool inputReady() {
                for (uint32_t attempt = 0; attempt < 100000; ++attempt) {
                    uint8_t status;

                    if (readPs2Port(0x64, status) == 0 && (status & 2U) == 0) {
                        return true;
                    }

                    asm volatile("pause");
                }

                return false;
            }

            /*
             * 立即检查控制器是否已有可读输出，不阻塞等待。
             */
            bool outputReady() {
                uint8_t status;

                return readPs2Port(0x64, status) == 0 && (status & 1U) != 0;
            }

            /*
             * 有限次等待数据就绪并读取，设备无响应时返回失败，避免初始化永久卡住。
             */
            bool readData(uint8_t& value) {
                for (uint32_t attempt = 0; attempt < 100000; ++attempt) {
                    if (outputReady()) {
                        return readPs2Port(0x60, value) == 0;
                    }

                    asm volatile("pause");
                }

                return false;
            }

            /*
             * 等待输入缓冲区空闲后，向控制端口发送命令字节。
             */
            bool writeCommand(uint8_t value) {
                return inputReady() && writePs2Port(0x64, value) == 0;
            }

            /*
             * 等待输入缓冲区空闲后，向数据端口发送一个字节。
             */
            bool writeData(uint8_t value) {
                return inputReady() && writePs2Port(0x60, value) == 0;
            }

            /*
             * 向键盘发送命令，并要求收到 0xfa 确认后才视为成功。
             */
            bool sendKeyboard(uint8_t value) {
                uint8_t acknowledgement;

                return writeData(value) && readData(acknowledgement) &&
                       acknowledgement == 0xfa;
            }

            /*
             * 先发鼠标转发命令，再发送数据并检查确认，避免把鼠标命令误送给键盘。
             */
            bool sendMouse(uint8_t value) {
                uint8_t acknowledgement;

                return writeCommand(0xd4) && writeData(value) &&
                       readData(acknowledgement) && acknowledgement == 0xfa;
            }

            /*
             * 按鼠标协议先发送设置采样率命令，再发送采样率参数，两步都要求确认。
             */
            bool setMouseSampleRate(uint8_t rate) {
                return sendMouse(0xf3) && sendMouse(rate);
            }

            /*
             * 通过指定采样率序列尝试启用滚轮，再查询设备编号；协商不支持时由调用者保留普通模式。
             */
            bool enableMouseWheel() {
                // 连续设置 200、100、80
                // 是鼠标协议约定的滚轮协商序列，并不是普通的最终采样设置。
                if (!setMouseSampleRate(200) || !setMouseSampleRate(100) ||
                    !setMouseSampleRate(80) || !sendMouse(0xf2)) {
                    return false;
                }

                // 必须查询到编号三才能按四字节包解析，确认后再设置正常使用的采样率。
                uint8_t deviceId;

                return readData(deviceId) && deviceId == 3 &&
                       setMouseSampleRate(200);
            }

            uint8_t configuration_   = 0;
            uint8_t mousePacketSize_ = 3;
    };

    static Controller controller;
    static ::task::SpinLock controllerLock;

    /*
     * 在控制器锁内初始化 PS/2，成功返回鼠标包长度，失败返回接口错误。
     */
    int initPs2() {
        ::task::InterruptSpinLockGuard guard(controllerLock);

        return controller.init() ? controller.mousePacketSize() : -11;
    }

    /*
     * 在控制器锁内读取一个待处理字节及其来源，供设备服务分派事件。
     */
    bool readPs2(uint8_t& value, bool& mouse) {
        ::task::InterruptSpinLockGuard guard(controllerLock);

        return controller.readInterruptByte(value, mouse);
    }

}  // namespace device
