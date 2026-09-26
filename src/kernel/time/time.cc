#include "kernel/time.h"

#include "kernel/atomic.h"
#include "kernel/cpu.h"
#include "kernel/serial.h"
#include "kernel/spinlock.h"
#include "kernel/thread.h"

namespace {

constexpr uint64_t nanosecondsPerSecond = 1000000000ULL;
constexpr uint64_t maximumTimeValue = ~0ULL;
kernel::Atomic<uint64_t> lastMonotonicNanoseconds{0};
kernel::SpinLock rtcLock;
uint64_t tscFrequency;
uint64_t bootTsc;
uint64_t wallClockBaseNanoseconds;

void cpuid(uint32_t leaf, uint32_t subleaf, uint32_t& eax, uint32_t& ebx,
           uint32_t& ecx, uint32_t& edx) {
    asm volatile("cpuid"
                 : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                 : "a"(leaf), "c"(subleaf));
}

uint64_t readTsc() {
    uint32_t low;
    uint32_t high;
    asm volatile("lfence; rdtsc" : "=a"(low), "=d"(high) : : "memory");
    return (static_cast<uint64_t>(high) << 32) | low;
}

uint8_t readPort8(uint16_t port) {
    uint8_t value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void writePort8(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

uint64_t calibrateTscWithPit() {
    constexpr uint16_t pitCount = 11932;
    const uint8_t gate = readPort8(0x61);
    writePort8(0x61, static_cast<uint8_t>((gate & ~0x02U) | 0x01U));
    writePort8(0x43, 0xb0);
    writePort8(0x42, static_cast<uint8_t>(pitCount));
    writePort8(0x42, static_cast<uint8_t>(pitCount >> 8));
    const uint64_t start = readTsc();
    uint32_t timeout = 100000000;
    while ((readPort8(0x61) & 0x20U) == 0 && timeout-- != 0) {
        asm volatile("pause");
    }
    const uint64_t elapsed = readTsc() - start;
    writePort8(0x61, gate);
    return timeout == 0 ? 0 : elapsed * 100;
}

uint64_t detectTscFrequency() {
    uint32_t maximumLeaf;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    cpuid(0, 0, maximumLeaf, ebx, ecx, edx);
    if (maximumLeaf >= 0x15) {
        uint32_t denominator;
        uint32_t numerator;
        uint32_t crystalFrequency;
        cpuid(0x15, 0, denominator, numerator, crystalFrequency, edx);
        if (denominator != 0 && numerator != 0 && crystalFrequency != 0) {
            return (static_cast<uint64_t>(crystalFrequency) * numerator) /
                   denominator;
        }
    }
    if (maximumLeaf >= 0x16) {
        uint32_t baseMegahertz;
        cpuid(0x16, 0, baseMegahertz, ebx, ecx, edx);
        if (baseMegahertz != 0) {
            return static_cast<uint64_t>(baseMegahertz) * 1000000ULL;
        }
    }
    return calibrateTscWithPit();
}

uint8_t readRtcRegister(uint8_t index) {
    writePort8(0x70, static_cast<uint8_t>(index | 0x80U));
    return readPort8(0x71);
}

uint8_t fromBcd(uint8_t value) {
    return static_cast<uint8_t>((value & 0x0fU) + (value >> 4) * 10);
}

bool isLeapYear(uint32_t year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

uint64_t daysBeforeYear(uint32_t year) {
    const uint64_t previous = year - 1;
    return 365ULL * (year - 1970) + previous / 4 - 1969 / 4 -
           (previous / 100 - 1969 / 100) +
           (previous / 400 - 1969 / 400);
}

uint64_t readWallClockSeconds() {
    kernel::InterruptSpinLockGuard guard(rtcLock);
    while ((readRtcRegister(0x0a) & 0x80U) != 0) {
        asm volatile("pause");
    }
    uint8_t second = readRtcRegister(0x00);
    uint8_t minute = readRtcRegister(0x02);
    uint8_t hour = readRtcRegister(0x04);
    uint8_t day = readRtcRegister(0x07);
    uint8_t month = readRtcRegister(0x08);
    uint8_t year = readRtcRegister(0x09);
    const uint8_t status = readRtcRegister(0x0b);
    if ((status & 0x04U) == 0) {
        second = fromBcd(second);
        minute = fromBcd(minute);
        hour = static_cast<uint8_t>((hour & 0x80U) | fromBcd(hour & 0x7fU));
        day = fromBcd(day);
        month = fromBcd(month);
        year = fromBcd(year);
    }
    if ((status & 0x02U) == 0) {
        const bool afternoon = (hour & 0x80U) != 0;
        hour = static_cast<uint8_t>(hour & 0x7fU);
        if (afternoon && hour != 12) {
            hour = static_cast<uint8_t>(hour + 12);
        } else if (!afternoon && hour == 12) {
            hour = 0;
        }
    }
    const uint32_t fullYear = 2000U + year;
    constexpr uint8_t monthDays[] = {31, 28, 31, 30, 31, 30,
                                     31, 31, 30, 31, 30, 31};
    uint64_t days = daysBeforeYear(fullYear);
    for (uint8_t currentMonth = 1; currentMonth < month; ++currentMonth) {
        days += monthDays[currentMonth - 1];
        if (currentMonth == 2 && isLeapYear(fullYear)) {
            ++days;
        }
    }
    days += day - 1;
    return ((days * 24 + hour) * 60 + minute) * 60 + second;
}

uint64_t scaleTsc(uint64_t cycles) {
    const uint64_t seconds = cycles / tscFrequency;
    const uint64_t remainder = cycles % tscFrequency;
    return seconds * nanosecondsPerSecond +
           (remainder * nanosecondsPerSecond) / tscFrequency;
}

}  // namespace

namespace kernel {

void initTime() {
    if (!getCpuFeatures().tsc) {
        serialWrite("[TIME] TSC is unavailable\n");
        haltCpu();
    }
    rtcLock.initialize();
    tscFrequency = detectTscFrequency();
    if (tscFrequency == 0) {
        serialWrite("[TIME] unable to determine TSC frequency\n");
        haltCpu();
    }
    bootTsc = readTsc();
    wallClockBaseNanoseconds = readWallClockSeconds() * nanosecondsPerSecond;
    lastMonotonicNanoseconds.store(0, MemoryOrder::Release);
    serialWrite(getCpuFeatures().invariantTsc
                    ? "[TIME] invariant TSC monotonic clock initialized at "
                    : "[TIME] calibrated TSC monotonic clock initialized at ");
    serialWriteDecimal(tscFrequency);
    serialWrite(" Hz\n");
}

uint64_t getMonotonicTime() {
    const uint64_t current = scaleTsc(readTsc() - bootTsc);
    uint64_t previous = lastMonotonicNanoseconds.load(MemoryOrder::Acquire);
    while (current > previous &&
           !lastMonotonicNanoseconds.compareExchange(
               previous, current, MemoryOrder::Release, MemoryOrder::Acquire)) {
    }
    return current > previous ? current : previous;
}

uint64_t getWallClockTime() {
    const uint64_t monotonic = getMonotonicTime();
    return maximumTimeValue - wallClockBaseNanoseconds < monotonic
               ? maximumTimeValue
               : wallClockBaseNanoseconds + monotonic;
}

uint64_t getClockResolution() {
    return tscFrequency >= nanosecondsPerSecond
               ? 1
               : (nanosecondsPerSecond + tscFrequency - 1) / tscFrequency;
}

uint64_t millisecondsToNanoseconds(uint64_t milliseconds) {
    return milliseconds > maximumTimeValue / 1000000ULL
               ? maximumTimeValue
               : milliseconds * 1000000ULL;
}

uint64_t microsecondsToNanoseconds(uint64_t microseconds) {
    return microseconds > maximumTimeValue / 1000ULL
               ? maximumTimeValue
               : microseconds * 1000ULL;
}

uint64_t getCpuRuntime(const Thread* thread) {
    return thread == nullptr ? 0 : thread->runtime;
}

bool deadlineReached(uint64_t now, uint64_t deadline) {
    return static_cast<int64_t>(now - deadline) >= 0;
}

}  // namespace kernel
