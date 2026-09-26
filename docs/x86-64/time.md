# 时间子系统（src/kernel/time/）

本文档描述 `src/kernel/time/time.cc` 的实现：TSC 频率探测、RTC 墙钟读取与单调/墙钟时间接口。对应头文件 `include/kernel/time.h`。

## 1. 接口

```cpp
void initTime();
uint64_t getMonotonicTime();      // 单调时钟，纳秒
uint64_t getWallClockTime();      // 墙钟（Unix epoch 起），纳秒
uint64_t getClockResolution();    // 时钟分辨率，纳秒
uint64_t millisecondsToNanoseconds(uint64_t milliseconds);
uint64_t microsecondsToNanoseconds(uint64_t microseconds);
uint64_t getCpuRuntime(const Thread* thread);
bool deadlineReached(uint64_t now, uint64_t deadline);
```

## 2. 时钟源

单调时钟基于 **TSC**（`lfence; rdtsc` 序列化读取）。`initTime` 要求 CPU 具备 TSC 特性，否则停机。

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

结果存入 `wallClockBaseNanoseconds`，此后墙钟 = 基准 + 单调时间。

## 3. 时间计算

- `scaleTsc(cycles)`：先整除得秒，余数用 `remainder * 1e9 / tscFrequency` 计算，避免溢出。
- `getMonotonicTime`：`scaleTsc(readTsc() - bootTsc)`；用原子 CAS 循环（`lastMonotonicNanoseconds`）保证全局返回值单调不减，返回 `max(current, previous)`。
- `getWallClockTime`：`wallClockBaseNanoseconds + getMonotonicTime()`，溢出饱和到 `~0ULL`。
- `getClockResolution`：`tscFrequency >= 1e9` 时为 1 ns，否则 `ceil(1e9 / tscFrequency)`。
- `millisecondsToNanoseconds`/`microsecondsToNanoseconds`：带溢出饱和。
- `getCpuRuntime(thread)`：返回 `thread->runtime`（调度器累计的实际运行纳秒数）。
- `deadlineReached(now, deadline)`：以有符号差值判断 `(int64_t)(now - deadline) >= 0`，安全处理回绕。睡眠唤醒（`schedulerTick`）使用此函数。

## 4. 使用者

- 调度器：运行队列时钟、vruntime 计算、睡眠截止时间（见 [task.md](task.md)）。
- 系统调用：GetMonotonicTime（25）、GetWallClockTime（26）、GetClockResolution（47）（见 [syscall.md](syscall.md)）。
- 用户态 `os::getWallClockTime` 用于文件系统时间戳（ext3 create/remove 等）。
