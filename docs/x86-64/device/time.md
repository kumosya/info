# 时间与时钟

实现位于 `src/kernel/device/time/time.cc`，声明在 `include/kernel/time.h`，命名空间为 `kernel::time`。Clock 保存 TSC 频率、启动 TSC、RTC 墙钟基准、上一次单调时间的原子值和 RTC 锁。`clock()` 返回共用时钟对象。

## 初始化与时间来源

`initClock()` 调用 `Clock::init()`。初始化要求 CPU 支持 TSC，先确定频率，再记录启动计数并读取 RTC 作为墙钟基准。TSC 不存在或频率探测失败会输出诊断并停止启动。

频率优先使用 CPUID 0x15 的晶振频率与分子、分母计算；不具备有效数据时尝试 0x16 的基础 MHz，再回退到 PIT 通道 2 约 10 ms 的校准。`calibrateTscWithPit()` 保存并恢复控制端口，轮询有次数上限，校准失败返回零。

内部 `readTsc()` 使用 lfence 后接 rdtsc 读取计数，使读数与此前指令顺序更明确。`Clock::scaleTsc(cycles)` 将计数按整秒和余数换算为纳秒，避免把整个累计计数直接乘以十亿。

## RTC 墙钟基准

`Clock::readWallClockSeconds()` 在关闭本地中断并持 RTC 锁后访问 CMOS 索引和数据端口。它等待 RTC 更新标志清除，读取秒、分、时、日、月、年，并根据状态寄存器处理 BCD 和 12 小时制，最后换算为 Unix 秒。

`fromBcd(value)` 处理十进制编码，`isLeapYear(year)` 和 `daysBeforeYear(year)` 处理日历换算。当前年份按 2000 加两位年份解释，没有读取世纪寄存器；等待 RTC 更新结束也没有独立超时。实现不能被描述为适用于任意世纪或异常 RTC 的完整日历驱动。

RTC 只在初始化时取得墙钟基准；之后读取墙钟使用单调时间增量，不会每次重新访问 RTC。

## 公共函数

| 函数 | 作用与参数 |
| --- | --- |
| `Clock::getMonotonicTime()`、`getMonotonicTime()` | 返回从启动基准开始的纳秒数，以原子最大值防止返回值倒退。 |
| `Clock::getWallClockTime()`、`getWallClockTime()` | 返回墙钟基准加单调增量，溢出时饱和为最大值。 |
| `Clock::getClockResolution()`、`getClockResolution()` | 返回由 TSC 频率推算的纳秒分辨率，最小为 1 ns。 |
| `milliseconds2Nanoseconds(milliseconds)` | 毫秒到纳秒的饱和转换。 |
| `microseconds2Nanoseconds(microseconds)` | 微秒到纳秒的饱和转换。 |
| `getCpuRuntime(thread)` | 读取线程累计运行纳秒，空指针返回零。 |
| `deadlineReached(now, deadline)` | 用有符号差值比较当前时刻与截止时刻，要求两者差值处于可判定的半个计数范围内。 |

Clock 成员与同名独立函数使用同一对象，不是两套时钟。原子最大值能避免观察到时间倒退，但不证明不同 CPU 的硬件 TSC 完全同步；落后的读数可能暂时返回此前较大的值。

分辨率是计数换算粒度，不是线程调度周期，也不是定时唤醒精度。deadlineReached 只做判断，不登记定时器；实际超时必须由调度器或相应等待队列处理。

## 与调度和系统服务的关系

调度器使用单调时间结算运行时长、管理睡眠和 IPC 截止时间。system.kernel 提供用户态读取的桥接入口，见[系统信息服务](../task/sys.md)。这里没有设置系统时间、NTP 校时、时区管理或通用定时器对象。
