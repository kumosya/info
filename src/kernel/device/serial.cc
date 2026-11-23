/*
 * 提供轮询串口日志及不依赖普通输出锁的紧急诊断。
 */

#include "kernel/device.h"
#include "kernel/sync.h"

namespace device {

    constexpr uint16_t serialPort = 0x3f8;
    static task::SpinLock outputLock;

    /*
     * 向串口寄存器端口写入一个字节。
     */
    static inline void writePort(uint16_t port, uint8_t value) {
        asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
    }

    /*
     * 读取串口寄存器端口的一个字节。
     */
    static inline uint8_t readPort(uint16_t port) {
        uint8_t value;
        asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));

        return value;
    }

    /*
     * 有限次等待发送器空闲后写出字符，不因串口无响应无限等待；调用者负责需要的输出互斥。
     */
    static void writeCharacter(char value) {
        for (uint32_t timeout = 0; timeout < 100000; ++timeout) {
            if ((readPort(serialPort + 5) & 0x20) != 0) {
                break;
            }
        }

        writePort(serialPort, static_cast<uint8_t>(value));
    }

    /*
     * 配置 COM1 的分频、八位数据格式、FIFO 和调制解调控制，为启动日志准备串口。
     */
    void initSerial() {
        // 初始化采用轮询发送，先关闭串口中断，再打开分频寄存器访问窗口。
        writePort(serialPort + 1, 0x00);
        writePort(serialPort + 3, 0x80);
        writePort(serialPort + 0, 0x01);
        writePort(serialPort + 1, 0x00);

        // 分频写好后恢复数据窗口，设为八数据位、无校验、一停止位，并初始化 FIFO
        // 与调制解调控制。
        writePort(serialPort + 3, 0x03);
        writePort(serialPort + 2, 0xc7);
        writePort(serialPort + 4, 0x0b);
    }

    /*
     * 在输出锁内发送一个字符，避免多 CPU 同时改写串口数据寄存器。
     */
    void serialWrite(char value) {
        ::task::InterruptSpinLockGuard guard(outputLock);
        writeCharacter(value);
    }

    /*
     * 在同一个输出锁范围内发送完整字符串，换行前补回车，空指针直接忽略。
     */
    void serialWrite(const char* value) {
        if (value == nullptr) {
            return;
        }

        // 整段字符串在同一把锁内发送，避免多个 CPU
        // 把文字交错；换行补回车以兼容串口终端。
        ::task::InterruptSpinLockGuard guard(outputLock);

        while (*value != '\0') {
            if (*value == '\n') {
                writeCharacter('\r');
            }

            writeCharacter(*value++);
        }
    }

    /*
     * 把 64 位数格式化成带前缀的固定十六进制字符串，再走普通串口输出。
     */
    void serialWriteHex(uint64_t value) {
        static constexpr char digits[] = "0123456789abcdef";
        char text[19]                  = "0x";
        uint32_t index                 = 2;

        for (int shift = 60; shift >= 0; shift -= 4) {
            text[index++] = digits[(value >> shift) & 0xf];
        }

        device::serialWrite(text);
    }

    /*
     * 把无符号整数逐位转成十进制，先收集倒序数字再反向输出，零值单独处理。
     */
    void serialWriteDecimal(uint64_t value) {
        char digits[20];
        uint32_t count = 0;

        if (value == 0) {
            device::serialWrite('0');

            return;
        }

        // 除十取余先得到低位数字，暂存在数组中；随后倒序输出才能恢复正常十进制顺序。
        while (value != 0 && count < sizeof(digits)) {
            digits[count++] = static_cast<char>('0' + (value % 10));
            value /= 10;
        }

        while (count != 0) {
            device::serialWrite(digits[--count]);
        }
    }

    /*
     * 不取普通输出锁的紧急字符串输出，供 NMI
     * 或锁损坏时仍能报告故障；并发输出可能交错。
     */
    void emergencyWrite(const char* value) {
        // 紧急输出不取普通串口锁：NMI
        // 或故障可能恰好打断持锁者，等锁会让诊断永远卡住。
        if (!value) {
            return;
        }

        while (*value) {
            if (*value == '\n') {
                writeCharacter('\r');
            }

            writeCharacter(*value++);
        }
    }

    /*
     * 把整数格式化为固定十六进制文本，并通过不依赖锁的紧急路径输出。
     */
    void emergencyHex(uint64_t value) {
        constexpr char digits[] = "0123456789abcdef";
        char text[19]           = "0x";

        for (unsigned i = 0; i < 16; ++i) {
            text[i + 2] = digits[(value >> (60 - i * 4)) & 15];
        }

        emergencyWrite(text);
    }

}  // namespace device
