/*
 * 发现并启动其他 CPU，协调跨 CPU 调度与地址缓存刷新。
 */

#include "kernel/smp.h"

#include "kernel/acpi.h"
#include "kernel/boot.h"
#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/interrupt.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/sync.h"
#include "kernel/syscall.h"
#include "kernel/task.h"

namespace cpu {

    constexpr uint32_t maximumSupportedCpus = 64;
    constexpr uint64_t directMapBase        = 0xffff800000000000ULL;
    constexpr uint64_t trampolineAddress    = 0x8000;
    constexpr uint8_t trampolineVector      = trampolineAddress >> 12;

    /* ACPI 中断控制器表头，后接变长 CPU 或控制器记录。 */
    struct [[gnu::packed]] Madt {
            acpi::AcpiSdtHeader header;
            uint32_t localApicAddress;
            uint32_t flags;
    };

    /* MADT 变长条目的公共头，必须先检查长度才能解释后续字段。 */
    struct [[gnu::packed]] MadtEntryHeader {
            uint8_t type;
            uint8_t length;
    };

    /* 传统本地 APIC 的 CPU 身份与启用状态。 */
    struct [[gnu::packed]] MadtLocalApic {
            MadtEntryHeader header;
            uint8_t taskorId;
            uint8_t apicId;
            uint32_t flags;
    };

    /* 使用较宽 APIC 编号的 CPU 固件记录。 */
    struct [[gnu::packed]] MadtLocalX2Apic {
            MadtEntryHeader header;
            uint16_t reserved;
            uint32_t x2ApicId;
            uint32_t flags;
            uint32_t taskorId;
    };

    static CpuDescriptor* cpuDescriptors;
    static uint32_t discoveredCpuCount;
    static ::Atomic<uint32_t> onlineCpuCount{0};
    static task::SpinLock tlbShootdownLock;
    static ::Atomic<uint64_t> tlbShootdownAddress{0};
    static ::Atomic<uint64_t> tlbShootdownSize{4096};
    static ::Atomic<uint32_t> tlbShootdownAcknowledgements{0};

    extern "C" uint8_t apTrampolineStart;
    extern "C" uint8_t apTrampolineEnd;
    extern "C" uint64_t apStartupCr3;
    extern "C" uint64_t apStartupStack;
    extern "C" uint64_t apStartupCpuLocal;
    extern "C" uint64_t apStartupEntry;

    /*
     * 清零多核描述记录，后续初始化只发布已明确填入的字段。
     */
    static void clearMemory(void* address, uint64_t size) {
        uint8_t* bytes = static_cast<uint8_t*>(address);

        for (uint64_t index = 0; index < size; ++index) {
            bytes[index] = 0;
        }
    }

    template <typename Type>

    /*
     * 根据符号相对启动跳板起点的偏移，找到复制到低地址后的同一个参数位置。
     */
    static Type* trampolinePointer(Type* symbol) {
        const uint64_t offset = reinterpret_cast<uint64_t>(symbol) -
                                reinterpret_cast<uint64_t>(&apTrampolineStart);

        return reinterpret_cast<Type*>(directMapBase + trampolineAddress +
                                       offset);
    }

    /*
     * 通过有限次暂停指令为处理器启动协议留出等待间隔，不使用尚未可用的睡眠调度。
     */
    static void startupDelay(uint32_t iterations) {
        for (uint32_t index = 0; index < iterations; ++index) {
            asm volatile("pause");
        }
    }

    /*
     * 检查固件处理器条目的启用位，只对固件允许使用的处理器建立启动记录。
     */
    static bool taskorEnabled(uint32_t flags) { return (flags & 1U) != 0; }

    /*
     * 扫描 MADT 的普通 APIC 和 x2APIC
     * 条目，统计已启用处理器；遇到损坏长度时停止遍历。
     */
    static uint32_t countEnabledTaskors(const Madt* madt) {
        uint32_t count = 0;
        const uint8_t* cursor =
            reinterpret_cast<const uint8_t*>(madt) + sizeof(Madt);
        const uint8_t* end =
            reinterpret_cast<const uint8_t*>(madt) + madt->header.length;

        // 普通 APIC 与 x2APIC
        // 条目布局不同；两种都统计，但损坏长度不能继续遍历。
        while (cursor + sizeof(MadtEntryHeader) <= end) {
            const MadtEntryHeader* header =
                reinterpret_cast<const MadtEntryHeader*>(cursor);

            if (header->length < sizeof(MadtEntryHeader) ||
                cursor + header->length > end) {
                break;
            }

            if (header->type == 0 && header->length >= sizeof(MadtLocalApic) &&
                taskorEnabled(
                    reinterpret_cast<const MadtLocalApic*>(cursor)->flags)) {
                ++count;
            } else if (header->type == 9 &&
                       header->length >= sizeof(MadtLocalX2Apic) &&
                       taskorEnabled(
                           reinterpret_cast<const MadtLocalX2Apic*>(cursor)
                               ->flags)) {
                ++count;
            }

            cursor += header->length;
        }

        return count;
    }

    /*
     * 填写一个 CPU 的内核编号、硬件编号和初始状态，供引导核与新启动核共享。
     */
    static void initializeDescriptor(CpuDescriptor& descriptor, uint32_t cpuId,
                                     uint32_t taskorId, uint32_t apicId,
                                     CpuState state) {
        descriptor.setCpuId(cpuId);
        descriptor.setTaskorId(taskorId);
        descriptor.setApicId(apicId);
        descriptor.setEnabled(true);
        descriptor.setLocalData(nullptr);
        descriptor.getState().store(state, ::MemoryOrder::Relaxed);
        descriptor.getStartupStage().store(0, ::MemoryOrder::Relaxed);
    }

    /*
     * 收到跨核调度通知时只登记需要重调度，实际切换留到中断返回安全点。
     */
    static void rescheduleIpiHandler(Context*) { task::requestReschedule(); }

    /*
     * 在接收 CPU 上逐页清除指定虚拟范围的旧地址翻译，完成后增加确认数。
     */
    static void tlbShootdownIpiHandler(Context*) {
        const uint64_t address =
            tlbShootdownAddress.load(::MemoryOrder::Acquire);
        const uint64_t size = tlbShootdownSize.load(::MemoryOrder::Acquire);

        // 页表改变不会自动清除其他 CPU 的旧翻译，接收方必须逐页执行 INVLPG。
        for (uint64_t offset = 0; offset < size; offset += 4096) {
            asm volatile("invlpg (%0)" : : "r"(address + offset) : "memory");
        }

        // 清除完成后才发布确认；发起方等齐确认后才能安全复用原来的物理页面。
        tlbShootdownAcknowledgements.fetchAdd(1, ::MemoryOrder::Release);
    }

    /*
     * 读取 MADT 并建立 CPU 描述表，引导 CPU
     * 固定为编号零，登记重调度与地址缓存更新中断。
     */
    void initSmp(const BootInfo* bootInfo) {
        // 固件信息缺失时仍允许只用引导 CPU
        // 运行，发现数量同时受内核支持上限限制。
        const Madt* madt =
            reinterpret_cast<const Madt*>(acpi::Tables(bootInfo).find("APIC"));

        if (madt == nullptr || madt->header.length < sizeof(Madt)) {
            device::serialWrite(
                "[SMP] ACPI MADT unavailable; using BSP only\n");
            discoveredCpuCount = 1;
        } else {
            discoveredCpuCount = countEnabledTaskors(madt);

            if (discoveredCpuCount == 0) {
                discoveredCpuCount = 1;
            } else if (discoveredCpuCount > maximumSupportedCpus) {
                discoveredCpuCount = maximumSupportedCpus;
            }
        }

        // CPU 记录统一分配；引导 CPU 固定占零号并立即标为在线，其他 CPU
        // 之后逐个启动。
        cpuDescriptors = static_cast<CpuDescriptor*>(
            mm::allocateHeap(sizeof(CpuDescriptor) * discoveredCpuCount));

        if (cpuDescriptors == nullptr) {
            device::serialWrite("[SMP] unable to allocate CPU descriptors\n");
            cpu::halt();
        }

        clearMemory(cpuDescriptors, sizeof(CpuDescriptor) * discoveredCpuCount);
        initializeDescriptor(cpuDescriptors[0], 0, 0,
                             cpu::getCurrent()->getApicId(), CpuState::Online);
        cpuDescriptors[0].setLocalData(cpu::getCurrent());

        // 再遍历一次把固件身份转成内核编号；引导 CPU
        // 不重复加入，当前发送方式只接受八位 APIC 目标。
        uint32_t nextCpuId = 1;

        if (madt != nullptr) {
            const uint8_t* cursor =
                reinterpret_cast<const uint8_t*>(madt) + sizeof(Madt);
            const uint8_t* end =
                reinterpret_cast<const uint8_t*>(madt) + madt->header.length;

            while (cursor + sizeof(MadtEntryHeader) <= end &&
                   nextCpuId < discoveredCpuCount) {
                const MadtEntryHeader* header =
                    reinterpret_cast<const MadtEntryHeader*>(cursor);

                if (header->length < sizeof(MadtEntryHeader) ||
                    cursor + header->length > end) {
                    break;
                }

                uint32_t taskorId = 0;
                uint32_t apicId   = 0xffffffffU;
                uint32_t flags    = 0;

                if (header->type == 0 &&
                    header->length >= sizeof(MadtLocalApic)) {
                    const MadtLocalApic* entry =
                        reinterpret_cast<const MadtLocalApic*>(cursor);
                    taskorId = entry->taskorId;
                    apicId   = entry->apicId;
                    flags    = entry->flags;
                } else if (header->type == 9 &&
                           header->length >= sizeof(MadtLocalX2Apic)) {
                    const MadtLocalX2Apic* entry =
                        reinterpret_cast<const MadtLocalX2Apic*>(cursor);
                    taskorId = entry->taskorId;
                    apicId   = entry->x2ApicId;
                    flags    = entry->flags;
                }

                if (taskorEnabled(flags)) {
                    if (apicId == cpu::getCurrent()->getApicId()) {
                        cpuDescriptors[0].setTaskorId(taskorId);
                    } else if (apicId <= 0xff) {
                        initializeDescriptor(cpuDescriptors[nextCpuId],
                                             nextCpuId, taskorId, apicId,
                                             CpuState::Offline);
                        ++nextCpuId;
                    }
                }

                cursor += header->length;
            }
        }

        // 最终数量以实际接受的记录为准，再登记后续多核调度和页表更新要用的中断入口。
        discoveredCpuCount = nextCpuId == 0 ? 1 : nextCpuId;
        onlineCpuCount.store(1, MemoryOrder::Release);
        cpu::registerHandler(rescheduleIpiVector, rescheduleIpiHandler);
        cpu::registerHandler(tlbShootdownIpiVector, tlbShootdownIpiHandler);
        device::serialWrite("[SMP] discovered CPUs=");
        device::serialWriteDecimal(discoveredCpuCount);
        device::serialWrite(" BSP APIC ID=");
        device::serialWriteDecimal(cpu::getCurrent()->getApicId());
        device::serialWrite("\n");
    }

    /*
     * 复制低地址启动跳板，为每个 CPU 写好页表、栈和入口参数，再按 INIT
     * 与启动中断顺序逐个启动并等待确认。
     */
    void startApplicationTaskors() {
        if (discoveredCpuCount <= 1) {
            device::serialWrite("[SMP] no application taskors to start\n");

            return;
        }

        // 新 CPU
        // 从低物理地址开始，跳板必须能装进一页；临时恒等映射让它在切换页表后仍能继续执行。
        const uint64_t trampolineSize =
            reinterpret_cast<uint64_t>(&apTrampolineEnd) -
            reinterpret_cast<uint64_t>(&apTrampolineStart);

        if (trampolineSize > 4096 ||
            !mm::mapPage(trampolineAddress, trampolineAddress,
                         ::mm::pageWritable)) {
            device::serialWrite("[SMP] unable to map AP trampoline\n");

            return;
        }

        // 把链接在内核中的跳板原样复制到
        // 0x8000，后面写参数时也要换算到这个副本的位置。
        uint8_t* trampoline =
            reinterpret_cast<uint8_t*>(directMapBase + trampolineAddress);
        const uint8_t* source = &apTrampolineStart;

        for (uint64_t index = 0; index < trampolineSize; ++index) {
            trampoline[index] = source[index];
        }

        // 每轮只启动一个 CPU；跳板参数由所有 CPU 共用，必须等当前 CPU
        // 接手后再改给下一个。
        for (uint32_t cpuId = 1; cpuId < discoveredCpuCount; ++cpuId) {
            CpuDescriptor& descriptor = cpuDescriptors[cpuId];
            descriptor.setLocalData(
                LocalData::prepare(cpuId, descriptor.getApicId()));

            if (descriptor.getLocalData() == nullptr) {
                descriptor.getState().store(CpuState::Failed,
                                            MemoryOrder::Release);
                continue;
            }

            descriptor.getState().store(CpuState::Starting,
                                        MemoryOrder::Release);
            descriptor.getStartupStage().store(1, MemoryOrder::Release);

            // 将页表根、栈顶、CPU 局部对象和 C++ 入口交给汇编跳板，避免新 CPU
            // 依赖引导 CPU 的寄存器。
            *trampolinePointer(&apStartupCr3) = mm::getKernelAddressSpaceRoot();
            *trampolinePointer(&apStartupStack) = reinterpret_cast<uint64_t>(
                descriptor.getLocalData()->getKernelStack());
            *trampolinePointer(&apStartupCpuLocal) =
                reinterpret_cast<uint64_t>(descriptor.getLocalData());
            *trampolinePointer(&apStartupEntry) =
                reinterpret_cast<uint64_t>(cpu::applicationTaskorEntry);

            // 按启动协议先复位，再发送两次启动通知；通知向量对应低地址跳板所在的物理页。
            cpu::sendInitInterTaskorInterrupt(descriptor.getApicId());
            startupDelay(1000000);
            cpu::sendStartupInterTaskorInterrupt(descriptor.getApicId(),
                                                 trampolineVector);
            startupDelay(200000);
            cpu::sendStartupInterTaskorInterrupt(descriptor.getApicId(),
                                                 trampolineVector);

            // 有界等待对方发布在线状态；失败时打印最后阶段，帮助区分卡在汇编切换还是
            // C++ 初始化。
            uint32_t timeout = 100000000;

            while (descriptor.getState().load(MemoryOrder::Acquire) ==
                       CpuState::Starting &&
                   timeout-- != 0) {
                asm volatile("pause");
            }

            if (descriptor.getState().load(MemoryOrder::Acquire) !=
                CpuState::Online) {
                descriptor.getState().store(CpuState::Failed,
                                            MemoryOrder::Release);
                device::serialWrite("[SMP] AP startup failed cpu=");
                device::serialWriteDecimal(cpuId);
                device::serialWrite(" apic=");
                device::serialWriteDecimal(descriptor.getApicId());
                device::serialWrite(" stage=");
                device::serialWriteDecimal(
                    descriptor.getStartupStage().load(MemoryOrder::Acquire));
                device::serialWrite("\n");
            }
        }

        // 启动轮次结束后撤销临时低地址映射，正常运行只保留内核需要的高地址映射。
        mm::unmapPage(trampolineAddress);
        device::serialWrite("[SMP] online CPUs=");
        device::serialWriteDecimal(cpu::getOnlineCpuCount());
        device::serialWrite("\n");
    }

    /*
     * 返回已经发现且采用的 CPU 描述数量，不表示它们都已成功上线。
     */
    uint32_t getDiscoveredCpuCount() { return discoveredCpuCount; }

    /*
     * 原子读取已经完成初始化的在线 CPU 数量。
     */
    uint32_t getOnlineCpuCount() {
        return onlineCpuCount.load(MemoryOrder::Acquire);
    }

    /*
     * 按内核 CPU 编号取得描述记录，越界时返回空指针。
     */
    CpuDescriptor* getCpuDescriptor(uint32_t cpuId) {
        return cpuId < discoveredCpuCount ? &cpuDescriptors[cpuId] : nullptr;
    }

    /*
     * 取得指定 CPU 已准备的局部状态，尚未准备或编号无效时返回空指针。
     */
    LocalData* getLocalData(uint32_t cpuId) {
        CpuDescriptor* descriptor = cpu::getCpuDescriptor(cpuId);

        return descriptor == nullptr ? nullptr : descriptor->getLocalData();
    }

    /*
     * 仅对另一个已在线 CPU 发送重调度通知，本 CPU 的请求由本地调度路径处理。
     */
    void requestRemoteReschedule(uint32_t cpuId) {
        CpuDescriptor* descriptor = cpu::getCpuDescriptor(cpuId);

        if (descriptor != nullptr && cpuId != cpu::getCurrentId() &&
            descriptor->getState().load(MemoryOrder::Acquire) ==
                CpuState::Online) {
            cpu::sendInterTaskorInterrupt(descriptor->getApicId(),
                                          rescheduleIpiVector);
        }
    }

    /*
     * 串行发布一批页表缓存失效请求，通知其他在线 CPU
     * 并等待全部确认；返回后调用者才能安全复用相关页面。
     */
    void requestTlbShootdown(uint64_t virtualAddress, uint64_t size) {
        if (cpu::getOnlineCpuCount() <= 1) {
            return;
        }

        // 持有全局刷新锁期间不能被抢占；等待锁时不要先屏蔽中断，否则无法响应别人的跨核请求。

        // 先禁止抢占，防止持锁线程被挂起；等待锁时仍保持中断可用，否则可能无法回应别人的跨核请求而互等。
        cpu::disablePreemption();
        tlbShootdownLock.lock();

        // 拿到全局锁后才关本地中断，并发布本轮地址、长度及清零后的确认数，保证请求不会互相覆盖。
        const uint64_t flags = cpu::disableInterrupts();
        tlbShootdownAddress.store(virtualAddress, MemoryOrder::Release);
        tlbShootdownSize.store(size, MemoryOrder::Release);
        tlbShootdownAcknowledgements.store(0, MemoryOrder::Release);

        // 只通知其他在线 CPU，并计算本轮需要等待的确认数；当前 CPU
        // 的本地刷新由调用路径负责。
        uint32_t targets = 0;

        for (uint32_t cpuId = 0; cpuId < discoveredCpuCount; ++cpuId) {
            CpuDescriptor& descriptor = cpuDescriptors[cpuId];

            if (cpuId != cpu::getCurrentId() &&
                descriptor.getState().load(MemoryOrder::Acquire) ==
                    CpuState::Online) {
                ++targets;
                cpu::sendInterTaskorInterrupt(descriptor.getApicId(),
                                              tlbShootdownIpiVector);
            }
        }

        // 所有目标都确认刷新后才能返回；原有映射对应的物理页在这之前不能被重新分配。
        while (tlbShootdownAcknowledgements.load(MemoryOrder::Acquire) <
               targets) {
            asm volatile("pause");
        }

        // 先释放请求槽，再恢复原中断允许状态和抢占层数，结束这轮跨核同步。
        tlbShootdownLock.unlock();
        cpu::restoreInterrupts(flags);
        cpu::enablePreemption();
    }

    /*
     * 新 CPU 从汇编跳板进入 C++ 后的初始化入口。
     * 建立局部寄存器、描述符、系统调用和调度，再发布在线状态并退出启动线程。
     */
    extern "C" [[noreturn]] void applicationTaskorEntry(LocalData* cpu) {
        CpuDescriptor* descriptor =
            cpu == nullptr ? nullptr : cpu::getCpuDescriptor(cpu->getCpuId());

        if (descriptor == nullptr) {
            cpu::halt();
        }

        // 阶段编号供引导 CPU 超时诊断；先建立本 CPU 的 GS、描述符和
        // APIC，再装入中断与系统调用入口。
        descriptor->getStartupStage().store(2, MemoryOrder::Release);
        cpu->initApplicationTaskorCpu();
        descriptor->getStartupStage().store(3, MemoryOrder::Release);
        cpu::loadIdt();
        task::initSyscall();
        ::task::initThreads();

        // 调度数据准备完毕后才发布在线状态，避免其他 CPU
        // 把工作送给尚不能调度的新 CPU。
        task::initScheduler();
        descriptor->getStartupStage().store(4, MemoryOrder::Release);
        descriptor->getState().store(CpuState::Online, MemoryOrder::Release);
        onlineCpuCount.fetchAdd(1, MemoryOrder::Release);

        // 启动线程完成职责后退出，接下来的空闲时间由每 CPU 的空闲线程负责。
        ::task::exitCurrentThread(0);
        cpu::halt();
    }

}  // namespace cpu
