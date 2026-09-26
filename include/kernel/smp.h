#pragma once

#include <stdint.h>

#include "kernel/atomic.h"

namespace kernel {

struct BootInfo;
struct CpuLocalData;

enum class CpuState : uint8_t { Offline, Starting, Online, Stopping, Failed };

struct CpuDescriptor {
    uint32_t cpuId;
    uint32_t processorId;
    uint32_t apicId;
    bool enabled;
    Atomic<CpuState> state;
    Atomic<uint32_t> startupStage;
    CpuLocalData* localData;
};

constexpr uint8_t rescheduleIpiVector   = 0xf0;
constexpr uint8_t tlbShootdownIpiVector = 0xf1;

void initSmp(const BootInfo* bootInfo);
void startApplicationProcessors();
uint32_t getDiscoveredCpuCount();
uint32_t getOnlineCpuCount();
CpuDescriptor* getCpuDescriptor(uint32_t cpuId);
CpuLocalData* getCpuLocalData(uint32_t cpuId);
void requestRemoteReschedule(uint32_t cpuId);
void requestTlbShootdown(uint64_t virtualAddress);

extern "C" [[noreturn]] void applicationProcessorEntry(CpuLocalData* cpu);

}  // namespace kernel
