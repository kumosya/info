#pragma once

#include <cstdint>

#include "kernel/atomic.h"

namespace kernel {

struct BootInfo;
class CpuLocalData;

enum class CpuState : uint8_t { Offline, Starting, Online, Stopping, Failed };

class CpuDescriptor {
   public:
    uint32_t getCpuId() const { return cpuId; }
    uint32_t setCpuId(uint32_t value) {
        cpuId = value;
        return cpuId;
    }
    uint32_t setTaskorId(uint32_t value) {
        taskorId = value;
        return taskorId;
    }
    uint32_t getApicId() const { return apicId; }
    uint32_t setApicId(uint32_t value) {
        apicId = value;
        return apicId;
    }
    bool setEnabled(bool value) {
        enabled = value;
        return enabled;
    }
    Atomic<CpuState>& getState() { return state; }
    const Atomic<CpuState>& getState() const { return state; }
    Atomic<uint32_t>& getStartupStage() { return startupStage; }
    const Atomic<uint32_t>& getStartupStage() const { return startupStage; }
    CpuLocalData* getLocalData() const { return localData; }
    CpuLocalData* setLocalData(CpuLocalData* value) {
        localData = value;
        return localData;
    }

   private:
    uint32_t cpuId;
    uint32_t taskorId;
    uint32_t apicId;
    bool enabled;
    Atomic<CpuState> state;
    Atomic<uint32_t> startupStage;
    CpuLocalData* localData;
};

constexpr uint8_t rescheduleIpiVector   = 0xf0;
constexpr uint8_t tlbShootdownIpiVector = 0xf1;

namespace smp {
    void initSmp(const BootInfo* bootInfo);
    void startApplicationTaskors();
    uint32_t getDiscoveredCpuCount();
    uint32_t getOnlineCpuCount();
    CpuDescriptor* getCpuDescriptor(uint32_t cpuId);
    CpuLocalData* getCpuLocalData(uint32_t cpuId);
    void requestRemoteReschedule(uint32_t cpuId);
    void requestTlbShootdown(uint64_t virtualAddress);

    extern "C" [[noreturn]] void applicationTaskorEntry(CpuLocalData* cpu);

}  // namespace smp

}  // namespace kernel
