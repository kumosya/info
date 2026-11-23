/*
 * 标定 TSC，读取日历基准并提供不倒退的单调时间。
 */

#include "kernel/time.h"

#include "kernel/atomic.h"
#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/sync.h"
#include "kernel/task.h"

namespace time {

    constexpr uint64_t nanoseconds_per_second = 1000000000ULL;
    constexpr uint64_t maximum_time_value     = ~0ULL;
    static Clock systemClock;

    /*
     * 读取指定 CPUID 功能页，用于发现硬件报告的时钟频率信息。
     */
    static void cpuid(uint32_t leaf, uint32_t subleaf, uint32_t& eax,
                      uint32_t& ebx, uint32_t& ecx, uint32_t& edx) {
        asm volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(leaf), "c"(subleaf));
    }

    /*
     * 用屏障约束读取顺序后取得时间戳计数器，组合高低位为 64 位周期数。
     */
    static uint64_t readTsc() {
        uint32_t low;
        uint32_t high;
        asm volatile("lfence; rdtsc" : "=a"(low), "=d"(high) : : "memory");

        return (static_cast<uint64_t>(high) << 32) | low;
    }

    /*
     * 从计时器或 RTC 使用的 I/O 端口读取一个字节。
     */
    static uint8_t readPort8(uint16_t port) {
        uint8_t value;
        asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));

        return value;
    }

    /*
     * 向计时器或 RTC 使用的 I/O 端口写一个字节。
     */
    static void writePort8(uint16_t port, uint8_t value) {
        asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
    }

    /*
     * 让 PIT 计数约十毫秒，同时测量 TSC
     * 增量以估算频率；等待有限次，结束后恢复原端口控制状态。
     */
    static uint64_t calibrateTscWithPit() {
        // 使用 PIT
        // 第二通道测量约十毫秒，保存原控制位并关闭扬声器输出，测量结束再还原。
        constexpr uint16_t pit_count = 11932;
        const uint8_t gate           = readPort8(0x61);
        writePort8(0x61, static_cast<uint8_t>((gate & ~0x02U) | 0x01U));
        writePort8(0x43, 0xb0);
        writePort8(0x42, static_cast<uint8_t>(pit_count));
        writePort8(0x42, static_cast<uint8_t>(pit_count >> 8));

        // 记录计数器首尾差值，等待有次数上限；测得十毫秒内的周期数乘一百近似每秒频率。
        const uint64_t start = readTsc();
        uint32_t timeout     = 100000000;

        while ((readPort8(0x61) & 0x20U) == 0 && timeout-- != 0) {
            asm volatile("pause");
        }

        const uint64_t elapsed = readTsc() - start;
        writePort8(0x61, gate);

        return timeout == 0 ? 0 : elapsed * 100;
    }

    /*
     * 优先读取硬件给出的 TSC 比例或基础频率，缺失时回退到 PIT 实测标定。
     */
    static uint64_t detectTscFrequency() {
        uint32_t maximum_leaf;
        uint32_t ebx;
        uint32_t ecx;
        uint32_t edx;
        cpuid(0, 0, maximum_leaf, ebx, ecx, edx);

        // 优先使用晶振频率与 TSC 比率，所需字段都非零才有效。
        if (maximum_leaf >= 0x15) {
            uint32_t denominator;
            uint32_t numerator;
            uint32_t crystal_frequency;
            cpuid(0x15, 0, denominator, numerator, crystal_frequency, edx);

            if (denominator != 0 && numerator != 0 && crystal_frequency != 0) {
                return (static_cast<uint64_t>(crystal_frequency) * numerator) /
                       denominator;
            }
        }

        // 比率信息不足时尝试基础 MHz 字段，仍无有效数据才实际测量 PIT。
        if (maximum_leaf >= 0x16) {
            uint32_t base_megahertz;
            cpuid(0x16, 0, base_megahertz, ebx, ecx, edx);

            if (base_megahertz != 0) {
                return static_cast<uint64_t>(base_megahertz) * 1000000ULL;
            }
        }

        return calibrateTscWithPit();
    }

    /*
     * 选择一个 CMOS 时钟寄存器并读取，选择时设置该端口的 NMI 屏蔽位。
     */
    static uint8_t readRtcRegister(uint8_t index) {
        writePort8(0x70, static_cast<uint8_t>(index | 0x80U));

        return readPort8(0x71);
    }

    /*
     * 把一个两位 BCD 数转换成普通整数，RTC 的十位与个位分别编码在高低半字节。
     */
    static uint8_t fromBcd(uint8_t value) {
        return static_cast<uint8_t>((value & 0x0fU) + (value >> 4) * 10);
    }

    /*
     * 按公历四年、百年和四百年规则判断闰年。
     */
    static bool isLeapYear(uint32_t year) {
        return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    }

    /*
     * 计算从 1970 年起到指定年份之前经过的天数，把平年和闰年一并计入。
     */
    static uint64_t daysBeforeYear(uint32_t year) {
        const uint64_t previous = year - 1;

        return 365ULL * (year - 1970) + previous / 4 - 1969 / 4 -
               (previous / 100 - 1969 / 100) + (previous / 400 - 1969 / 400);
    }

    /*
     * 在 RTC 锁内读取日期与时间，按硬件模式处理 BCD 和十二小时制，再换算成 1970
     * 年起的秒数。
     */
    uint64_t Clock::readWallClockSeconds() {
        // CMOS
        // 索引与数据端口共用，读取必须串行；先避开硬件正在更新日历的时间段。
        task::InterruptSpinLockGuard guard(rtc_lock);

        while ((readRtcRegister(0x0a) & 0x80U) != 0) {
            asm volatile("pause");
        }

        uint8_t second       = readRtcRegister(0x00);
        uint8_t minute       = readRtcRegister(0x02);
        uint8_t hour         = readRtcRegister(0x04);
        uint8_t day          = readRtcRegister(0x07);
        uint8_t month        = readRtcRegister(0x08);
        uint8_t year         = readRtcRegister(0x09);
        const uint8_t status = readRtcRegister(0x0b);

        // RTC 可以返回 BCD 或二进制，先按配置转成数值，并保留小时中的下午标记。
        if ((status & 0x04U) == 0) {
            second = fromBcd(second);
            minute = fromBcd(minute);
            hour = static_cast<uint8_t>((hour & 0x80U) | fromBcd(hour & 0x7fU));
            day  = fromBcd(day);
            month = fromBcd(month);
            year  = fromBcd(year);
        }

        // 十二小时制的十二点上午是零时，十二点下午是十二时，其余下午时刻加十二。
        if ((status & 0x02U) == 0) {
            const bool afternoon = (hour & 0x80U) != 0;
            hour                 = static_cast<uint8_t>(hour & 0x7fU);

            if (afternoon && hour != 12) {
                hour = static_cast<uint8_t>(hour + 12);
            } else if (!afternoon && hour == 12) {
                hour = 0;
            }
        }

        // 当前按 2000 加两位年份解释，再累加自 1970 年以来的整年、整月和闰日。
        const uint32_t full_year       = 2000U + year;
        constexpr uint8_t month_days[] = {31, 28, 31, 30, 31, 30,
                                          31, 31, 30, 31, 30, 31};
        uint64_t days                  = daysBeforeYear(full_year);

        for (uint8_t current_month = 1; current_month < month;
             ++current_month) {
            days += month_days[current_month - 1];

            if (current_month == 2 && isLeapYear(full_year)) {
                ++days;
            }
        }

        days += day - 1;

        return ((days * 24 + hour) * 60 + minute) * 60 + second;
    }

    /*
     * 把 TSC 周期数换算成纳秒，先分离整秒与余数，降低直接大数相乘的溢出风险。
     */
    uint64_t Clock::scaleTsc(uint64_t cycles) const {
        // 拆成整秒与不足一秒的余数再换算纳秒，减少直接拿大周期数乘十亿带来的溢出风险。
        const uint64_t seconds   = cycles / tsc_frequency;
        const uint64_t remainder = cycles % tsc_frequency;

        return seconds * nanoseconds_per_second +
               (remainder * nanoseconds_per_second) / tsc_frequency;
    }

    /*
     * 返回全局系统时钟对象。
     */
    Clock& clock() { return systemClock; }

    /*
     * 检测并标定 TSC，记录启动计数与 RTC 时间基准，初始化全局单调时间下限。
     */
    void Clock::init() {
        if (!cpu::getCpuFeatures().hasTsc()) {
            device::serialWrite("[TIME] TSC is unavailable\n");
            cpu::halt();
        }

        // 确认频率可用后记录启动 TSC 和 RTC
        // 基准；之后每次查询无需重新访问慢速日历端口。
        rtc_lock.init();
        tsc_frequency = detectTscFrequency();

        if (tsc_frequency == 0) {
            device::serialWrite("[TIME] unable to determine TSC frequency\n");
            cpu::halt();
        }

        boot_tsc = readTsc();
        wall_clock_base_nanoseconds =
            readWallClockSeconds() * nanoseconds_per_second;
        last_monotonic_nanoseconds.store(0, MemoryOrder::Release);
        device::serialWrite(
            cpu::getCpuFeatures().hasInvariantTsc()
                ? "[TIME] invariant TSC monotonic clock initialized at "
                : "[TIME] calibrated TSC monotonic clock initialized at ");
        device::serialWriteDecimal(tsc_frequency);
        device::serialWrite(" Hz\n");
    }

    /*
     * 从启动 TSC 增量计算时间，并原子维护不下降的全局下限，使多 CPU
     * 读数不会回退。
     */
    uint64_t Clock::getMonotonicTime() {
        const uint64_t current = scaleTsc(readTsc() - boot_tsc);

        // 多 CPU 读到的 TSC
        // 可能略有差异，用原子保存已发布的最大值，保证调用者不会看到时间倒退。
        uint64_t previous =
            last_monotonic_nanoseconds.load(MemoryOrder::Acquire);

        // 只尝试推进时间；比较交换失败会更新
        // previous，若别人已经发布更大值就直接沿用它。
        while (current > previous &&
               !last_monotonic_nanoseconds.compareExchange(
                   previous, current, MemoryOrder::Release,
                   MemoryOrder::Acquire)) {
        }

        return current > previous ? current : previous;
    }

    /*
     * 以启动时的日历基准加单调增量计算墙上时间，超过可表示范围时饱和到最大值。
     */
    uint64_t Clock::getWallClockTime() {
        const uint64_t monotonic = getMonotonicTime();

        // 日历基准加启动后经过的时间；加法超出表示范围时饱和到最大值，避免回绕成很小的时间。
        const uint64_t base =
            maximum_time_value - wall_clock_base_nanoseconds < monotonic
                ? maximum_time_value
                : wall_clock_base_nanoseconds + monotonic;
        const int64_t adjustment =
            wall_adjustment_nanoseconds.load(MemoryOrder::Acquire);
        if (adjustment < 0) {
            const uint64_t amount =
                static_cast<uint64_t>(-(adjustment + 1)) + 1;
            return amount > base ? 0 : base - amount;
        }
        return static_cast<uint64_t>(adjustment) > maximum_time_value - base
                   ? maximum_time_value
                   : base + static_cast<uint64_t>(adjustment);
    }

    /* 只调整日历时间基准，单调时间和调度器定时不受校时影响。 */
    bool Clock::setWallClockTime(uint64_t nanoseconds) {
        const uint64_t monotonic = getMonotonicTime();
        if (nanoseconds > INT64_MAX || monotonic > INT64_MAX ||
            wall_clock_base_nanoseconds > INT64_MAX - monotonic)
            return false;
        const uint64_t base = wall_clock_base_nanoseconds + monotonic;
        wall_adjustment_nanoseconds.store(
            static_cast<int64_t>(nanoseconds) - static_cast<int64_t>(base),
            MemoryOrder::Release);
        return true;
    }

    /*
     * 按 TSC 频率估算纳秒分辨率，向上取整且最小为一纳秒。
     */
    uint64_t Clock::getClockResolution() const {
        return tsc_frequency >= nanoseconds_per_second
                   ? 1
                   : (nanoseconds_per_second + tsc_frequency - 1) /
                         tsc_frequency;
    }

    /*
     * 初始化全局系统时钟，供启动流程调用。
     */
    void initClock() { clock().init(); }

    /*
     * 读取全局单调时钟的纳秒数。
     */
    uint64_t getMonotonicTime() { return clock().getMonotonicTime(); }

    /*
     * 读取全局墙上时间的纳秒数。
     */
    uint64_t getWallClockTime() { return clock().getWallClockTime(); }

    /*
     * 读取全局时钟分辨率。
     */
    uint64_t getClockResolution() { return clock().getClockResolution(); }

    /*
     * 把毫秒换算成纳秒，输入过大时饱和而不是溢出成很小的时长。
     */
    uint64_t milliseconds2Nanoseconds(uint64_t milliseconds) {
        return milliseconds > maximum_time_value / 1000000ULL
                   ? maximum_time_value
                   : milliseconds * 1000000ULL;
    }

    /*
     * 把微秒换算成纳秒，超过表示范围时返回最大值。
     */
    uint64_t microseconds2Nanoseconds(uint64_t microseconds) {
        return microseconds > maximum_time_value / 1000ULL
                   ? maximum_time_value
                   : microseconds * 1000ULL;
    }

    /*
     * 读取线程累计 CPU 执行时间，空线程指针返回零。
     */
    uint64_t getCpuRuntime(const ::task::Thread* thread) {
        return thread == nullptr ? 0 : thread->getRuntime();
    }

    /*
     * 通过有符号时间差判断截止时间是否已到，供定时等待统一使用。
     */
    bool deadlineReached(uint64_t now, uint64_t deadline) {
        return static_cast<int64_t>(now - deadline) >= 0;
    }

}  // namespace time
