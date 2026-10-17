# 时间支持（src/kernel/device/time.cc）

本文档描述 `src/kernel/device/time.cc` 的实现：TSC 频率探测、RTC 墙钟读取与单调/墙钟时间接口。对应头文件 `include/kernel/time.h`。

## 1. 接口

所有函数位于 `namespace kernel::time`：

```cpp
namespace kernel::time {

void init();
uint64_t getMonotonicTime();      // 单调时钟，纳秒
uint64_t getWallClockTime();      // 墙钟（Unix epoch 起），纳秒
uint64_t getClockResolution();    // 时钟分辨率，纳秒
uint64_t milliseconds2Nanoseconds(uint64_t milliseconds);
uint64_t microseconds2Nanoseconds(uint64_t microseconds);
uint64_t getCpuRuntime(const Thread* thread);
bool deadlineReached(uint64_t now, uint64_t deadline);

}  // namespace kernel::time
```

## 2. 时钟源

单调时钟基于 **TSC**（`lfence; rdtsc` 序列化读取）。`init()` 要求 CPU 具备 TSC 特性，否则停机。

### TSC 频率探测（三级回退）

1. **CPUID leaf 0x15**（TSC/晶振比）：`crystalFrequency * numerator / denominator`。
2. **CPUID leaf 0x16**（处理器基频）：`baseMegahertz * 1,000,000`。
3. **PIT 校准**（`calibrateTscWithPit`）：用 PIT 通道 2（端口 0x42/0x43，门控端口 0x61）计时约 10 ms（计数值 11932），测量 TSC 增量后 ×100 得每秒频率。超时返回 0。

频率为 0 则停机。日志区分 `invariant TSC`（CPUID 0x80000007 EDX bit 8）与 calibrated TSC。

### 墙钟基准（RTC）

启动时通过 CMOS RTC（端口 0x70/0x71，读时禁 NMI）读取一次墙钟：

- 等待更新中标志（寄存器 0x0a bit 7）清零；
- 读秒/分/时/日/月/年（寄存器 0x00/0x02/0x04/0x07/0x08/0x09）与状态寄存器 0x0b；
- 非二进制模式做 BCD 转换；12 小时制转 24 小时制；
- 年份按 `2000 + year` 解释；按闰年规则累加天数，换算为 Unix epoch 秒数。

结果存入 `wall_clock_base_nanoseconds`，此后墙钟 = 基准 + 单调时间。

## 3. 时间计算

- `scaleTsc(cycles)`：先整除得秒，余数用 `remainder * 1e9 / tsc_frequency` 计算，避免溢出。
- `getMonotonicTime`：`scaleTsc(readTsc() - boot_tsc)`；用原子 CAS 循环（`last_monotonic_nanoseconds`）保证全局返回值单调不减，返回 `max(current, previous)`。
- `getWallClockTime`：`wall_clock_base_nanoseconds + getMonotonicTime()`，溢出饱和到 `~0ULL`。
- `getClockResolution`：`tsc_frequency >= 1e9` 时为 1 ns，否则 `ceil(1e9 / tsc_frequency)`。
- `milliseconds2Nanoseconds`/`microseconds2Nanoseconds`：带溢出饱和。
- `getCpuRuntime(thread)`：返回 `thread->getRuntime()`（调度器累计的实际运行纳秒数）。
- `deadlineReached(now, deadline)`：以有符号差值判断 `(int64_t)(now - deadline) >= 0`，安全处理回绕。睡眠唤醒（`schedulerTick`）使用此函数。

## 4. 内部变量

匿名命名空间中的静态变量使用 snake_case：

| 变量名 | 类型 | 说明 |
|---|---|---|
| `tsc_frequency` | `uint64_t` | TSC 每秒频率 |
| `boot_tsc` | `uint64_t` | 启动时 TSC 基准值 |
| `wall_clock_base_nanoseconds` | `uint64_t` | RTC 墙钟基准（纳秒） |
| `last_monotonic_nanoseconds` | `Atomic<uint64_t>` | 单调时钟 CAS 保护 |
| `rtc_lock` | `SpinLock` | RTC 寄存器访问锁 |

## 5. 使用者

- 调度器：运行队列时钟、vruntime 计算、睡眠截止时间（见 [task.md](task/task.md)）。
- 系统调用：GetMonotonicTime（61）、GetWallClockTime（62）、GetClockResolution（63）（见 [syscall.md](task/syscall.md)）。
- POSIX 时间函数由 libc 使用相同的 OS 系统调用接口实现。
