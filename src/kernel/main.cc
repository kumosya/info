#include "kernel/boot.h"
#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/elf.h"
#include "kernel/heap.h"
#include "kernel/interrupt.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/process.h"
#include "kernel/scheduler.h"
#include "kernel/serial.h"
#include "kernel/smp.h"
#include "kernel/syscall.h"
#include "kernel/sync.h"
#include "kernel/thread.h"
#include "kernel/time.h"
#include "kernel/user.h"

namespace {

constexpr uint64_t directMapBase      = 0xffff800000000000ULL;
constexpr uint64_t userProbeCode      = 0x0000000000400000ULL;
constexpr uint64_t userProbeStackBase = 0x0000000000800000ULL;
constexpr uint64_t pageSize           = 4096;

uint32_t schedulerProbeCount;
uint32_t ipcProbeResult;
uint32_t smpProbeCount;
kernel::Endpoint* ipcProbeEndpoint;

extern "C" uint8_t userSyscallProbeStart;
extern "C" uint8_t userSyscallProbeEnd;

kernel::Process* createSyscallProbe() {
    kernel::Process* process =
        kernel::Process::create("syscall-probe", kernel::getCurrentProcess());
    if (process == nullptr) {
        kernel::serialWrite("[SYSCALL] probe process creation failed\n");
        return nullptr;
    }
    const uint64_t codePhysical  = kernel::allocatePhysicalPage();
    const uint64_t stackPhysical = kernel::allocatePhysicalPage();
    const uint64_t codeSize =
        reinterpret_cast<uint64_t>(&userSyscallProbeEnd) -
        reinterpret_cast<uint64_t>(&userSyscallProbeStart);
    if (codePhysical == 0 || stackPhysical == 0) {
        kernel::serialWrite("[SYSCALL] probe page allocation failed\n");
        return nullptr;
    }
    if (codeSize > pageSize) {
        kernel::serialWrite("[SYSCALL] probe code exceeds one page\n");
        return nullptr;
    }
    if (!kernel::mapPageInAddressSpace(
            process->addressSpace().rootPhysicalAddress(), userProbeCode,
            codePhysical, kernel::pageUser)) {
        kernel::serialWrite("[SYSCALL] probe code mapping failed\n");
        return nullptr;
    }
    if (!kernel::mapPageInAddressSpace(
            process->addressSpace().rootPhysicalAddress(), userProbeStackBase,
            stackPhysical,
            kernel::pageUser | kernel::pageWritable | kernel::pageNoExecute)) {
        kernel::serialWrite("[SYSCALL] probe stack mapping failed\n");
        return nullptr;
    }
    uint8_t* code = reinterpret_cast<uint8_t*>(directMapBase + codePhysical);
    const uint8_t* source = &userSyscallProbeStart;
    for (uint64_t index = 0; index < pageSize; ++index) {
        code[index] = index < codeSize ? source[index] : 0;
    }
    uint8_t* stack = reinterpret_cast<uint8_t*>(directMapBase + stackPhysical);
    for (uint64_t index = 0; index < pageSize; ++index) {
        stack[index] = 0;
    }
    kernel::Thread* thread = kernel::createUserThread(
        process, userProbeCode, userProbeStackBase + pageSize);
    if (thread == nullptr) {
        kernel::serialWrite("[SYSCALL] probe thread creation failed\n");
        return nullptr;
    }
    if (kernel::getOnlineCpuCount() > 1 &&
        !kernel::setThreadAffinity(thread, 1ULL << 1)) {
        kernel::serialWrite("[SYSCALL] probe thread affinity failed\n");
        return nullptr;
    }
    return process;
}

void schedulerProbe(void*) {
    //kernel::serialWrite("[THREAD] probe ");
    //kernel::serialWriteDecimal(reinterpret_cast<uint64_t>(argument));
    //kernel::serialWrite(" running\n");
    kernel::yieldThread();
    __atomic_add_fetch(&schedulerProbeCount, 1U, __ATOMIC_RELAXED);
    //kernel::serialWrite("[THREAD] probe completed\n");
}

void smpSchedulerProbe(void*) {
    kernel::serialWrite("[SMP] scheduled probe on CPU ");
    kernel::serialWriteDecimal(kernel::getCurrentCpuId());
    kernel::serialWrite("\n");
    __atomic_store_n(&smpProbeCount, 1U, __ATOMIC_RELEASE);
}

void ipcServerProbe(void*) {
    kernel::IpcMessage request{};
    request.length = 0xffffffffU;
    if (kernel::receiveMessage(ipcProbeEndpoint, &request) != 0 ||
        request.type != 0x41 || request.length != 1 ||
        request.data[0] != 0x1234 || request.senderTid == 0) {
        __atomic_store_n(&ipcProbeResult, 2U, __ATOMIC_RELEASE);
        return;
    }
    kernel::IpcMessage reply{};
    reply.type    = 0x42;
    reply.length  = 1;
    reply.data[0] = request.data[0] + 1;
    if (kernel::replyMessage(ipcProbeEndpoint, &reply) != 0) {
        __atomic_store_n(&ipcProbeResult, 2U, __ATOMIC_RELEASE);
    }
    //kernel::serialWrite("[IPC] server probe completed\n");
}

void ipcClientProbe(void*) {
    kernel::IpcMessage request{};
    request.type    = 0x41;
    request.length  = 1;
    request.data[0] = 0x1234;
    kernel::IpcMessage reply{};
    if (kernel::callService(ipcProbeEndpoint, &request, &reply) == 0 &&
        reply.type == 0x42 && reply.length == 1 && reply.data[0] == 0x1235) {
        __atomic_store_n(&ipcProbeResult, 1U, __ATOMIC_RELEASE);
    } else {
        __atomic_store_n(&ipcProbeResult, 2U, __ATOMIC_RELEASE);
    }
    //kernel::serialWrite("[IPC] client probe completed\n");
}

}  // namespace

namespace kernel {

void kernelMain(const BootInfo* bootInfo) {
    //serialWrite("[KERNEL] kernelMain()\n");
    //serialWrite("[KERNEL] architecture initialized\n");
    initPhysicalMemory(bootInfo);
    initVirtualMemory(bootInfo);
    initKernelHeap();
    volatile uint64_t* heapProbe =
        static_cast<volatile uint64_t*>(kernelAllocate(sizeof(uint64_t)));
    if (heapProbe == nullptr) {
        serialWrite("[MEMORY] kernel heap initialization failed\n");
        while (true) {
            asm volatile("cli; hlt");
        }
    }
    *heapProbe = 0x6f73696e666fULL;
    initCpu();
    initTime();
    initIdt();
    initInterruptControllers(bootInfo);
    initDeviceSubsystem(bootInfo);
    initSmp(bootInfo);
    initThreadSubsystem();
    initScheduler();
    initProcessSubsystem();
    runSynchronizationSelfTest();
    initSyscall();
    startApplicationProcessors();
    if (getOnlineCpuCount() > 1) {
        Thread* smpProbe = createThread(nullptr, smpSchedulerProbe, nullptr);
        if (smpProbe == nullptr || !setThreadAffinity(smpProbe, 1ULL << 1)) {
            serialWrite("[SMP] unable to create CPU affinity probe\n");
            haltCpu();
        }
    } else {
        __atomic_store_n(&smpProbeCount, 1U, __ATOMIC_RELEASE);
    }
    Process* processProbe =
        Process::create("address-space-probe", getCurrentProcess());
    if (processProbe == nullptr ||
        processProbe->addressSpace().rootPhysicalAddress() ==
            getKernelAddressSpaceRoot() ||
        !runElfLoaderValidationSelfTest(*processProbe)) {
        serialWrite("[PROCESS] address-space self-test failed\n");
        haltCpu();
    }
    //serialWrite("[PROCESS] independent address-space self-test complete\n");
    //serialWrite("[ELF] malformed image validation self-test complete\n");
    processProbe->destroy();
    Process* syscallProbe = createSyscallProbe();
    if (syscallProbe == nullptr) {
        serialWrite("[SYSCALL] unable to create Ring 3 probe\n");
        haltCpu();
    }
    initIpc();
    ipcProbeEndpoint = createEndpoint();
    if (ipcProbeEndpoint == nullptr ||
        createThread(getCurrentProcess(), ipcServerProbe, nullptr) == nullptr ||
        createThread(getCurrentProcess(), ipcClientProbe, nullptr) == nullptr) {
        serialWrite("[IPC] unable to create IPC probes\n");
        haltCpu();
    }
    if (createThread(nullptr, schedulerProbe, reinterpret_cast<void*>(1)) ==
            nullptr ||
        createThread(nullptr, schedulerProbe, reinterpret_cast<void*>(2)) ==
            nullptr) {
        serialWrite("[THREAD] unable to create scheduler probes\n");
        haltCpu();
    }
    enableInterrupts();
    //serialWrite("[KERNEL] startup probes scheduled asynchronously\n");
    //serialWrite("[KERNEL] memory initialization complete\n");
    if (startInitialUserland(bootInfo) == nullptr) {
        haltCpu();
    }
    //serialWrite("[KERNEL] entering idle loop\n");

    while (true) {
        asm volatile("hlt");
    }
}

}  // namespace kernel

extern "C" void kernelEntry(uint64_t multibootAddress,
                            uint64_t pageTableRoot,
                            uint64_t bootstrapStart,
                            uint64_t bootstrapEnd) {
    kernel::initSerial();
    //kernel::serialWrite("[BOOT] osinfo starting\n");
    //kernel::serialWrite("[BOOT] Entered x86-64 long mode\n");

    kernel::BootInfo bootInfo{};
    if (!kernel::parseMultibootInfo(multibootAddress, &bootInfo)) {
        kernel::serialWrite("[BOOT] invalid Multiboot2 information\n");
        while (true) {
            asm volatile("cli; hlt");
        }
    }
    bootInfo.pageTableRoot = pageTableRoot;
    bootInfo.bootstrapStart = bootstrapStart;
    bootInfo.bootstrapEnd = bootstrapEnd;

    //kernel::serialWrite("[BOOT] Multiboot2 information received\n");
    //kernel::serialWrite("[BOOT] memory map found\n");
    kernel::kernelMain(&bootInfo);
}
