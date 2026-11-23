/*
 * 多 CPU 启动与跨核协调。
 */

#pragma once

#include <cstdint>

#include "kernel/atomic.h"

class BootInfo;

namespace cpu {

    class LocalData;

    enum class CpuState : uint8_t {
        Offline,
        Starting,
        Online,
        Stopping,
        Failed
    };

    /* 记录一个 CPU 的启动标识和当前状态，供多核启动协调。 */
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
            ::Atomic<CpuState>& getState() { return state; }
            const ::Atomic<CpuState>& getState() const { return state; }
            ::Atomic<uint32_t>& getStartupStage() { return startupStage; }
            const ::Atomic<uint32_t>& getStartupStage() const {
                return startupStage;
            }
            LocalData* getLocalData() const { return localData; }
            LocalData* setLocalData(LocalData* value) {
                localData = value;
                return localData;
            }

        private:
            uint32_t cpuId;
            uint32_t taskorId;
            uint32_t apicId;
            bool enabled;
            ::Atomic<CpuState> state;
            ::Atomic<uint32_t> startupStage;
            LocalData* localData;
    };

    constexpr uint8_t rescheduleIpiVector   = 0xf0;
    constexpr uint8_t tlbShootdownIpiVector = 0xf1;

    void initSmp(const ::BootInfo* bootInfo);
    void startApplicationTaskors();
    uint32_t getDiscoveredCpuCount();
    uint32_t getOnlineCpuCount();
    CpuDescriptor* getCpuDescriptor(uint32_t cpuId);
    LocalData* getLocalData(uint32_t cpuId);
    void requestRemoteReschedule(uint32_t cpuId);
    void requestTlbShootdown(uint64_t virtualAddress, uint64_t size = 4096);

    extern "C" [[noreturn]] void applicationTaskorEntry(LocalData* cpu);

}  // namespace cpu
